/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RendererMMAL.h"

#include "ServiceBroker.h"
#include "cores/VideoPlayer/Buffers/VideoBufferPoolMMAL.h"
#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodec.h"
#include "cores/VideoPlayer/VideoRenderers/RenderCapture.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFactory.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFlags.h"
#include "settings/DisplaySettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "settings/lib/Setting.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"

#include <interface/mmal/util/mmal_default_components.h>
#include <interface/mmal/util/mmal_util.h>
#include <interface/mmal/util/mmal_util_params.h>
#include <interface/mmal/vc/mmal_vc_msgs.h>

using namespace MMAL;
using namespace std::chrono_literals;

#define MMAL_COMPONENT_DEFAULT_ISP_CONVERTER "vc.ril.isp"

const std::string SETTING_VIDEOPLAYER_USEMMALRENDERER = "videoplayer.usemmaldecoderforhw";

CBaseRenderer* CRendererMMAL::Create(CVideoBuffer* buffer)
{
  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
          SETTING_VIDEOPLAYER_USEMMALRENDERER) &&
      buffer)
  {
    auto buf = dynamic_cast<CVideoBufferMMAL*>(buffer);

    if (buf)
    {
      auto winSystem =
          static_cast<KODI::WINDOWING::DMX::CWinSystemDmx*>(CServiceBroker::GetWinSystem());
      if (winSystem)
        return new CRendererMMAL(winSystem);
    }
  }
  return nullptr;
}

void CRendererMMAL::Register()
{
  auto winSystem =
      dynamic_cast<KODI::WINDOWING::DMX::CWinSystemDmx*>(CServiceBroker::GetWinSystem());
  if (winSystem)
  {
    CServiceBroker::GetSettingsComponent()
        ->GetSettings()
        ->GetSetting(SETTING_VIDEOPLAYER_USEMMALRENDERER)
        ->SetVisible(true);
    VIDEOPLAYER::CRendererFactory::RegisterRenderer("mmal", CRendererMMAL::Create);
    return;
  }
}

void CRendererMMAL::ProcessControlCallback(MMALPort port, MMALBufferHeader header)
{
  CRendererMMAL* renderer = static_cast<CRendererMMAL*>((void*)port->userdata);
  if (renderer && header->cmd == MMAL_EVENT_ERROR)
  {
    MMALStatus status = *(MMALStatus*)header->data;
    if (status != MMAL_EAGAIN)
    {
      renderer->m_state = MRS_ERROR;
      CLog::Log(LOGWARNING, "CRendererMMAL::{} - renderer error reported: {}", __FUNCTION__,
                mmal_status_to_string(status));
    }
  }
  mmal_buffer_header_release(header);
}

void CRendererMMAL::ProcessInputCallback(MMALPort port, MMALBufferHeader header)
{
  CRendererMMAL* renderer = static_cast<CRendererMMAL*>((void*)port->userdata);
  if (renderer)
  {
    CVideoBufferMMAL* buffer = static_cast<CVideoBufferMMAL*>(header->user_data);
    if (buffer)
    {
      std::unique_lock<CCriticalSection> lock(renderer->m_bufferLock);
      buffer->SetRendering(false);
      lock.unlock();
      renderer->m_bufferCondition.notify();
    }
    else
      mmal_buffer_header_release(header);
  }
  else
    mmal_buffer_header_release(header);
}

CRendererMMAL::CRendererMMAL(KODI::WINDOWING::DMX::CWinSystemDmx* winSystem)
  : CBaseRenderer::CBaseRenderer(), m_winSystem(winSystem)
{
  MMALStatus status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER, &m_renderer);
  if (status != MMAL_SUCCESS)
  {
    CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to create renderer component", __FUNCTION__);
    return;
  }
  status = mmal_component_create(MMAL_COMPONENT_DEFAULT_ISP_CONVERTER, &m_isp);
  if (status != MMAL_SUCCESS)
  {
    CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to create renderer component", __FUNCTION__);
    return;
  }
  if (m_renderer->is_enabled != 0)
    mmal_component_disable(m_renderer);
  if (m_isp->is_enabled != 0)
    mmal_component_disable(m_isp);

  *(int*)((uint8_t*)m_renderer->priv + 28) = VCOS_THREAD_PRI_REALTIME;
  *(int*)((uint8_t*)m_isp->priv + 28) = VCOS_THREAD_PRI_ABOVE_NORMAL;

  m_bufferCount = MMAL_RENDERER_NUM_BUFFERS - 2;
  m_port = m_renderer->input[0];
  m_port->userdata = (MMALPortUserData)this;
  m_port->buffer_num = m_bufferCount;
  m_port->buffer_num_min = 2;
  m_port->buffer_num_recommended = m_bufferCount;
  m_portFormat = mmal_format_alloc();
  m_portFormat->extradata = nullptr;
  m_portFormat->extradata_size = 0;
  m_format = AV_PIX_FMT_NONE;

  for (int i = 0; i < MMAL_RENDERER_NUM_BUFFERS; i++)
    m_buffers[i] = nullptr;

  mmal_port_parameter_set_uint32(m_port, MMAL_PARAMETER_EXTRA_BUFFERS, 0);
  mmal_port_parameter_set_boolean(m_port, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);

  MMALParameterHeader parameter =
      mmal_port_parameter_alloc_get(m_port, MMAL_PARAMETER_SUPPORTED_ENCODINGS, 0, &status);
  if (status == MMAL_SUCCESS)
  {
    uint32_t* formats = (uint32_t*)((uint8_t*)parameter + sizeof(MMALParameterHeader));
    for (uint32_t i = 0; i < 24; i++)
    {
      if (i < (parameter->size - sizeof(MMALParameterHeader)) / 4)
        m_renderFormats[i] = formats[i];
      else
        m_renderFormats[i] = MMAL_ENCODING_UNKNOWN;
    }
    mmal_port_parameter_free(parameter);
  }
  m_renderer->control->userdata = (MMALPortUserData)this;
  status = mmal_port_enable(m_renderer->control, CRendererMMAL::ProcessControlCallback);
  if (status != MMAL_SUCCESS)
  {
    CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to enable renderer control port", __FUNCTION__);
    return;
  }
  MMALPort ispPort = m_isp->input[0];
  ispPort->userdata = (MMALPortUserData)this;
  ispPort->buffer_num = m_bufferCount;
  ispPort->buffer_num_min = 2;
  ispPort->buffer_num_recommended = m_bufferCount;

  mmal_port_parameter_set_uint32(ispPort, MMAL_PARAMETER_EXTRA_BUFFERS, 0);
  mmal_port_parameter_set_boolean(ispPort, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);

  parameter =
      mmal_port_parameter_alloc_get(ispPort, MMAL_PARAMETER_SUPPORTED_ENCODINGS, 0, &status);
  if (status == MMAL_SUCCESS)
  {
    uint32_t* formats = (uint32_t*)((uint8_t*)parameter + sizeof(MMALParameterHeader));
    for (uint32_t i = 0; i < 64; i++)
    {
      if (i < (parameter->size - sizeof(MMALParameterHeader)) / 4)
        m_ispFormats[i] = formats[i];
      else
        m_ispFormats[i] = MMAL_ENCODING_UNKNOWN;
    }
    mmal_port_parameter_free(parameter);
  }
  m_isp->control->userdata = (MMALPortUserData)this;
  status = mmal_port_enable(m_isp->control, CRendererMMAL::ProcessControlCallback);
  if (status != MMAL_SUCCESS)
  {
    CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to enable isp control port", __FUNCTION__);
    return;
  }
  m_state = MRS_INITIALIZED;
}

CRendererMMAL::~CRendererMMAL()
{
  m_state = MRS_DESTROYING;

  Flush(false);

  std::unique_lock<CCriticalSection> lock(m_portLock);

  if (m_port->is_enabled != 0)
  {
    CLog::Log(LOGDEBUG, "CRendererMMAL::{} - disabling input port", __FUNCTION__);
    if (mmal_port_disable(m_port) == MMAL_SUCCESS)
    {
      CLog::Log(LOGDEBUG, "CRendererMMAL::{} - disabled input port", __FUNCTION__);
      m_port->userdata = nullptr;
    }
    else
      CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to disable renderer port", __FUNCTION__);
  }

  if (m_renderer->control->is_enabled != 0)
  {
    if (mmal_port_disable(m_renderer->control) == MMAL_SUCCESS)
      m_renderer->control->userdata = nullptr;
    else
      CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to disable renderer control port",
                __FUNCTION__);
  }

  if (m_isp->control->is_enabled != 0)
  {
    if (mmal_port_disable(m_isp->control) == MMAL_SUCCESS)
      m_isp->control->userdata = nullptr;
    else
      CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to disable isp control port", __FUNCTION__);
  }

  if (m_connection)
  {
    if (mmal_connection_disable(m_connection) == MMAL_SUCCESS)
      mmal_connection_destroy(m_connection);
    m_connection = nullptr;
  }

  if (m_isp->is_enabled != 0)
    mmal_component_disable(m_isp);

  if (m_renderer->is_enabled != 0)
    mmal_component_disable(m_renderer);

  if (m_portFormat)
  {
    mmal_format_free(m_portFormat);
    m_portFormat = nullptr;
  }

  if (mmal_component_release(m_isp) != MMAL_SUCCESS)
    CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to release isp component", __FUNCTION__);

  if (mmal_component_release(m_renderer) != MMAL_SUCCESS)
    CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to release renderer component", __FUNCTION__);

  m_renderer = nullptr;
  m_isp = nullptr;

  m_state = MRS_UNINITIALIZED;
}

bool CRendererMMAL::Configure(const VideoPicture& picture, float fps, unsigned int orientation)
{
  CVideoBufferMMAL* buffer = nullptr;

  if ((buffer = dynamic_cast<CVideoBufferMMAL*>(picture.videoBuffer)) != NULL)
  {
    MMALFormat format = buffer->GetPortFormat();
    uint32_t inputFormat = MMAL_ENCODING_UNKNOWN;
    uint32_t outputFormat = MMAL_ENCODING_UNKNOWN;

    for (int i = 0; i < 24; i++)
    {
      if (m_renderFormats[i] == MMAL_ENCODING_UNKNOWN)
        break;
      else if (format->encoding == m_renderFormats[i])
      {
        inputFormat = m_renderFormats[i];
        break;
      }
    }
    if (inputFormat == MMAL_ENCODING_UNKNOWN)
    {
      for (int i = 0; i < 64; i++)
      {
        if (m_ispFormats[i] == MMAL_ENCODING_UNKNOWN)
          break;
        else if (format->encoding == m_ispFormats[i])
        {
          inputFormat = m_ispFormats[i];
          break;
        }
      }
      if (inputFormat == MMAL_ENCODING_YUVUV64_10 || inputFormat == MMAL_ENCODING_YUVUV64_16)
        outputFormat = MMAL_ENCODING_YUVUV128;
      else
        outputFormat = MMAL_ENCODING_I420;
    }
    else
    {
      outputFormat = inputFormat;
    }

    if (inputFormat == MMAL_ENCODING_UNKNOWN || outputFormat == MMAL_ENCODING_UNKNOWN)
    {
      //unsupported format
      return false;
    }
    if (mmal_format_compare(m_portFormat, format) != 0)
    {
      if (inputFormat == outputFormat)
        m_port = m_renderer->input[0];
      else
        m_port = m_isp->input[0];
      if (mmal_format_full_copy(m_portFormat, format) == MMAL_SUCCESS)
      {
        std::unique_lock<CCriticalSection> lock(m_portLock);
        mmal_format_copy(m_port->format, m_portFormat);
        if (inputFormat != outputFormat)
          m_port->format->es->video.color_space = MMAL_COLOR_SPACE_UNKNOWN;
        if (mmal_port_format_commit(m_port) != MMAL_SUCCESS)
        {
          CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to commit port format", __FUNCTION__);
          return false;
        }
      }
      else
      {
        CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to copy port format", __FUNCTION__);
        return false;
      }

      m_port->buffer_size = buffer->GetSize();
      m_port->buffer_num = m_bufferCount;

      if (inputFormat != outputFormat)
      {
        if (mmal_format_full_copy(m_isp->output[0]->format, m_portFormat) == MMAL_SUCCESS)
        {
          m_isp->output[0]->format->encoding = outputFormat;
          m_isp->output[0]->format->encoding_variant = MMAL_ENCODING_UNKNOWN;
          m_isp->output[0]->format->es->video.color_space = m_portFormat->es->video.color_space;
          if (outputFormat == MMAL_ENCODING_YUVUV128)
          {
            m_isp->output[0]->format->es->video.width =
                VCOS_ALIGN_UP(m_isp->output[0]->format->es->video.crop.width, 32);
            m_isp->output[0]->format->es->video.height =
                VCOS_ALIGN_UP(m_isp->output[0]->format->es->video.crop.height, 16);
            if ((m_isp->output[0]->format->flags &
                 MMAL_ES_FORMAT_FLAG_COL_FMTS_WIDTH_IS_COL_STRIDE) != 0)
              m_isp->output[0]->format->flags &= ~MMAL_ES_FORMAT_FLAG_COL_FMTS_WIDTH_IS_COL_STRIDE;
          }

          if (mmal_port_format_commit(m_isp->output[0]) != MMAL_SUCCESS)
          {
            CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to commit isp port format",
                      __FUNCTION__);
            return false;
          }
        }
        else
        {
          CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to copy isp port format", __FUNCTION__);
          return false;
        }
        m_isp->output[0]->buffer_size = m_isp->output[0]->buffer_size_recommended;
        m_isp->output[0]->buffer_num = m_bufferCount;
        if (mmal_connection_create(&m_connection, m_isp->output[0], m_renderer->input[0],
                                   MMAL_CONNECTION_FLAG_TUNNELLING) != MMAL_SUCCESS)
        {
          CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to create isp connection", __FUNCTION__);
          return false;
        }
      }
    }

    m_fps = fps;
    m_format = picture.videoBuffer->GetFormat();
    m_sourceWidth = picture.iWidth;
    m_sourceHeight = picture.iHeight;
    m_renderOrientation = orientation;

    m_iFlags = GetFlagsChromaPosition(picture.chroma_position) |
               GetFlagsColorMatrix(picture.color_space, picture.iWidth, picture.iHeight) |
               GetFlagsColorPrimaries(picture.color_primaries) |
               GetFlagsStereoMode(picture.stereoMode);

    CalculateFrameAspectRatio(picture.iDisplayWidth, picture.iDisplayHeight);
    SetViewMode(m_videoSettings.m_ViewMode);

    CDisplaySettings::GetInstance().SetPixelRatio(1.0);

    m_displayRegion.hdr.id = MMAL_PARAMETER_DISPLAYREGION;
    m_displayRegion.hdr.size = sizeof(MMAL_DISPLAYREGION_T);

    m_displayRegion.set = MMAL_DISPLAY_SET_LAYER;
    m_displayRegion.layer = 0;

    m_displayRegion.set |= MMAL_DISPLAY_SET_NUM;
    m_displayRegion.display_num = 2;

    m_displayRegion.set |= MMAL_DISPLAY_SET_ALPHA;
    m_displayRegion.alpha = 255 | MMAL_DISPLAY_ALPHA_FLAGS_DISCARD_LOWER_LAYERS;

    m_displayRegion.set |= MMAL_DISPLAY_SET_FULLSCREEN;
    m_displayRegion.fullscreen = MMAL_FALSE;

    m_displayRegion.set |= MMAL_DISPLAY_SET_NOASPECT;
    m_displayRegion.noaspect = MMAL_TRUE;

    m_displayRegion.set |= MMAL_DISPLAY_SET_MODE;
    m_displayRegion.mode = MMAL_DISPLAY_MODE_LETTERBOX;

    m_displayRegion.transform = MMAL_DISPLAY_DUMMY;

    m_state = MRS_CONFIGURED;

    ManageRenderArea();

    return true;
  }
}

void CRendererMMAL::ManageRenderArea()
{
  CBaseRenderer::ManageRenderArea();

  if (m_state == MRS_CONFIGURED || m_state == MRS_RENDERING)
  {
    MMAL_DISPLAYTRANSFORM_T transform = MMAL_DISPLAY_ROT0;

    if (m_renderOrientation > 0)
    {
      switch (m_renderOrientation)
      {
        case 270:
          transform = MMAL_DISPLAY_ROT90;
          break;
        case 180:
          transform = MMAL_DISPLAY_ROT180;
          break;
        case 90:
          transform = MMAL_DISPLAY_ROT270;
          break;
        default:
          transform = MMAL_DISPLAY_ROT0;
          break;
      }
    }

    if (m_displayRegion.transform != transform)
    {
      m_displayRegion.set |= MMAL_DISPLAY_SET_TRANSFORM;
      m_displayRegion.transform = transform;
    }

    if (m_displayRegion.src_rect.x != (int32_t)m_sourceRect.x1 ||
        m_displayRegion.src_rect.y != (int32_t)m_sourceRect.y1 ||
        m_displayRegion.src_rect.width != (int32_t)m_sourceRect.Width() ||
        m_displayRegion.src_rect.height != (int32_t)m_sourceRect.Height())
    {
      m_displayRegion.set |= MMAL_DISPLAY_SET_SRC_RECT;
      m_displayRegion.src_rect.x = m_sourceRect.x1;
      m_displayRegion.src_rect.y = m_sourceRect.y1;
      m_displayRegion.src_rect.width = m_sourceRect.Width();
      m_displayRegion.src_rect.height = m_sourceRect.Height();
    }

    if (m_displayRegion.dest_rect.x != (int32_t)m_destRect.x1 ||
        m_displayRegion.dest_rect.y != (int32_t)m_destRect.y1 ||
        m_displayRegion.dest_rect.width != (int32_t)m_destRect.Width() ||
        m_displayRegion.dest_rect.height != (int32_t)m_destRect.Height())
    {
      m_displayRegion.set |= MMAL_DISPLAY_SET_DEST_RECT;
      m_displayRegion.dest_rect.x = m_destRect.x1;
      m_displayRegion.dest_rect.y = m_destRect.y1;
      m_displayRegion.dest_rect.width = m_destRect.Width();
      m_displayRegion.dest_rect.height = m_destRect.Height();
    }

    if (m_displayRegion.set != MMAL_DISPLAY_SET_NONE)
    {
      // aspect is calculated by CBaseRenderer?
      m_displayRegion.set |= MMAL_DISPLAY_SET_PIXEL;
      m_displayRegion.pixel_x = 1;
      m_displayRegion.pixel_y = 1;

      std::unique_lock<CCriticalSection> lock(m_portLock);
      if (mmal_port_parameter_set(m_port, &m_displayRegion.hdr) == MMAL_SUCCESS)
        m_displayRegion.set = MMAL_DISPLAY_SET_NONE;
      else
        CLog::Log(LOGWARNING, "CRendererMMAL::{} - failed to configure display region",
                  __FUNCTION__);
    }
  }
}

bool CRendererMMAL::IsConfigured()
{
  return m_state != MRS_INITIALIZED && m_state != MRS_UNINITIALIZED && m_state != MRS_DESTROYING;
}

void CRendererMMAL::AddVideoPicture(const VideoPicture& picture, int index)
{
  CVideoBufferMMAL* buffer = nullptr;
  if ((buffer = dynamic_cast<CVideoBufferMMAL*>(picture.videoBuffer)) != NULL)
  {
    buffer->ReadPicture(picture);
    AcquireBuffer(buffer, index);
  }
}

void CRendererMMAL::Update()
{
  if (m_state == MRS_RENDERING)
    ManageRenderArea();
}

void CRendererMMAL::RenderUpdate(
    int index, int index2, bool clear, unsigned int flags, unsigned int alpha)
{
  MMALRendererState state = m_state;

  if (state == MRS_RENDERING)
  {
    if (!SendBuffer(index))
      ReleaseBuffer(index);
  }
  else if (state == MRS_CONFIGURED || state == MRS_FLUSHED)
  {
    ManageRenderArea();
    if (m_port->is_enabled == 0)
    {
      m_port->buffer_num = m_bufferCount;
      std::unique_lock<CCriticalSection> lock(m_portLock);
      if (mmal_port_enable(m_port, CRendererMMAL::ProcessInputCallback) == MMAL_SUCCESS)
      {
        if (m_connection)
        {
          mmal_connection_enable(m_connection);
          if (m_isp->is_enabled == 0)
            mmal_component_enable(m_isp);
        }
        if (m_renderer->is_enabled == 0)
          mmal_component_enable(m_renderer);
      }
    }
    if (!SendBuffer(index))
      ReleaseBuffer(index);
    else
      m_state = MRS_RENDERING;
  }
}

bool CRendererMMAL::Flush(bool saveBuffers)
{
  MMALRendererState state = m_state;
  bool flush = false;

  m_state = MRS_FLUSHING;
  std::unique_lock<CCriticalSection> lock(m_bufferLock);

  for (int i = 0; i < MMAL_RENDERER_NUM_BUFFERS; i++)
  {
    if (m_buffers[i] != nullptr)
    {
      if (m_buffers[i]->IsRendering() == false)
      {
        if (!saveBuffers)
        {
          m_buffers[i]->Release();
          m_buffers[i] = nullptr;
        }
      }
      else
        flush = true;
    }
  }

  if (state != MRS_FLUSHED && m_port->is_enabled != 0 && flush)
  {
    CLog::Log(LOGDEBUG, "CRendererMMAL::{} - flushing input port", __FUNCTION__);
    if (((MMALPortPrivate)m_port->priv)->pf_flush(m_port) != MMAL_SUCCESS)
      CLog::Log(LOGERROR, "CRendererMMAL::{} - failed to flush input port", __FUNCTION__);
    else
      CLog::Log(LOGDEBUG, "CRendererMMAL::{} - flushed input port", __FUNCTION__);
  }

  m_state = MRS_FLUSHED;

  return saveBuffers;
}

bool CRendererMMAL::SendBuffer(int index)
{
  std::unique_lock<CCriticalSection> lock(m_bufferLock);
  if (m_buffers[index] != nullptr)
  {
    if (m_buffers[index]->IsRendering())
      return true;

    MMALBufferHeader header = m_buffers[index]->GetHeader();
    if ((header->flags & MMAL_BUFFER_HEADER_FLAG_DROPPED) == 0)
    {
      m_buffers[index]->SetRendering(true);
      if (m_state == MRS_FLUSHED && (header->flags & MMAL_BUFFER_HEADER_FLAG_DISCONTINUITY) == 0)
        header->flags |= MMAL_BUFFER_HEADER_FLAG_DISCONTINUITY;
      MMALStatus status = mmal_port_send_buffer(m_port, header);
      if (status == MMAL_EAGAIN)
      {
        m_winSystem->WaitVerticalSync(m_winSystem->WaitVerticalSync(0) + 1, 1000 / m_fps);
        status = mmal_port_send_buffer(m_port, header);
      }
      if (status == MMAL_SUCCESS)
      {
        if (m_state == MRS_RENDERING)
          m_bufferCondition.wait(lock);
        return true;
      }
    }
    m_buffers[index]->SetRendering(false);
  }
  return false;
}

void CRendererMMAL::AcquireBuffer(CVideoBufferMMAL* buffer, int index)
{
  std::unique_lock<CCriticalSection> lock(m_bufferLock);
  if (m_buffers[index] != nullptr)
  {
    m_buffers[index]->Release();
    m_buffers[index] = nullptr;
  }
  buffer->Acquire(false);
  m_buffers[index] = buffer;
  m_buffers[index]->SetRendering(false);
}

void CRendererMMAL::ReleaseBuffer(int index)
{
  std::unique_lock<CCriticalSection> lock(m_bufferLock);
  if (m_buffers[index] != nullptr)
  {
    m_buffers[index]->Release();
    m_buffers[index] = nullptr;
  }
}

bool CRendererMMAL::NeedBuffer(int index)
{
  std::unique_lock<CCriticalSection> lock(m_bufferLock);
  bool result = false;
  if (m_buffers[index] != nullptr)
  {
    if (m_buffers[index]->IsRendering() == false)
    {
      m_buffers[index]->Release();
      m_buffers[index] = nullptr;
    }
    else
      result = true;
  }
  return result;
}

CRenderInfo CRendererMMAL::GetRenderInfo()
{
  CRenderInfo info;
  info.max_buffer_size = MMAL_RENDERER_NUM_BUFFERS;
  info.optimal_buffer_size = m_port->buffer_num_recommended;

  info.formats.push_back(AV_PIX_FMT_MMAL);
  info.formats.push_back(AV_PIX_FMT_YUV420P);
  info.formats.push_back(AV_PIX_FMT_YUVJ420P);
  info.formats.push_back(AV_PIX_FMT_YUV420P10);
  info.formats.push_back(AV_PIX_FMT_YUV420P12);
  info.formats.push_back(AV_PIX_FMT_YUV420P14);
  info.formats.push_back(AV_PIX_FMT_YUV420P16);
  info.formats.push_back(AV_PIX_FMT_SAND128);
  info.formats.push_back(AV_PIX_FMT_SAND64_10);
  info.formats.push_back(AV_PIX_FMT_SAND64_16);
  return info;
}

bool CRendererMMAL::ConfigChanged(const VideoPicture& picture)
{
  CVideoBufferMMAL* buffer = dynamic_cast<CVideoBufferMMAL*>(picture.videoBuffer);
  if (buffer)
    return mmal_format_compare(m_portFormat, buffer->GetPortFormat()) != 0;
  return true;
}

void CRendererMMAL::SetBufferSize(int numBuffers)
{
  if (numBuffers > MMAL_RENDERER_NUM_BUFFERS)
    m_bufferCount = MMAL_RENDERER_NUM_BUFFERS;
  else
    m_bufferCount = numBuffers;
}

bool CRendererMMAL::Supports(ERENDERFEATURE feature) const
{
  switch (feature)
  {
    case RENDERFEATURE_STRETCH:
    case RENDERFEATURE_ZOOM:
    case RENDERFEATURE_VERTICAL_SHIFT:
    case RENDERFEATURE_PIXEL_RATIO:
      return true;
    default:
      return false;
  }
}

bool CRendererMMAL::Supports(ESCALINGMETHOD method) const
{
  return method == VS_SCALINGMETHOD_AUTO;
}
