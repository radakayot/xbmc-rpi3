/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "VideoBufferPoolMMAL.h"

#include "ServiceBroker.h"
#include "threads/SingleLock.h"
#include "threads/Timer.h"
#include "utils/BufferObjectFactory.h"
#include "utils/log.h"
#include "windowing/dmx/WinSystemDmx.h"

#define MMAL_COMPONENT_DEFAULT_NULL_SINK "vc.null_sink"

using namespace MMAL;

uint32_t CVideoBufferPoolMMAL::TranslateFormat(AVPixelFormat format)
{
  switch (format)
  {
    case AV_PIX_FMT_MMAL:
      return MMAL_ENCODING_OPAQUE;
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
      return MMAL_ENCODING_I420;
    case AV_PIX_FMT_YUV420P10:
      return MMAL_ENCODING_I420_10;
    case AV_PIX_FMT_YUV420P12:
    case AV_PIX_FMT_YUV420P14:
    case AV_PIX_FMT_YUV420P16:
      return MMAL_ENCODING_I420_16;
    case AV_PIX_FMT_SAND128:
      return MMAL_ENCODING_YUVUV128;
    case AV_PIX_FMT_SAND64_10:
      return MMAL_ENCODING_YUVUV64_10;
    case AV_PIX_FMT_SAND64_16:
      return MMAL_ENCODING_YUVUV64_16;
    case AV_PIX_FMT_YUV411P:
    case AV_PIX_FMT_YUVJ411P:
      return MMAL_ENCODING_YV12;
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUVJ422P:
      return MMAL_ENCODING_I422;
    case AV_PIX_FMT_NV12:
      return MMAL_ENCODING_NV12;
    case AV_PIX_FMT_NV21:
      return MMAL_ENCODING_NV21;
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_RGB0:
      return MMAL_ENCODING_RGBA;
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_BGR0:
      return MMAL_ENCODING_BGRA;
    case AV_PIX_FMT_RGB24:
      return MMAL_ENCODING_RGB24;
    case AV_PIX_FMT_BGR24:
      return MMAL_ENCODING_BGR24;
    case AV_PIX_FMT_RGB565:
      return MMAL_ENCODING_RGB16;
    case AV_PIX_FMT_BGR565:
      return MMAL_ENCODING_BGR16;
    default:
      return MMAL_ENCODING_UNKNOWN;
  }
}

AVPixelFormat CVideoBufferPoolMMAL::TranslatePortFormat(uint32_t format)
{
  switch (format)
  {
    case MMAL_ENCODING_OPAQUE:
      return AV_PIX_FMT_MMAL;
    case MMAL_ENCODING_I420:
      return AV_PIX_FMT_YUV420P;
    case MMAL_ENCODING_I420_10:
      return AV_PIX_FMT_YUV420P10;
    case MMAL_ENCODING_I420_16:
      return AV_PIX_FMT_YUV420P16;
    case MMAL_ENCODING_YUVUV128:
      return AV_PIX_FMT_SAND128;
    case MMAL_ENCODING_YUVUV64_10:
      return AV_PIX_FMT_SAND64_10;
    case MMAL_ENCODING_YUVUV64_16:
      return AV_PIX_FMT_SAND64_16;
    case MMAL_ENCODING_YV12:
      return AV_PIX_FMT_YUV411P;
    case MMAL_ENCODING_I422:
      return AV_PIX_FMT_YUV422P;
    case MMAL_ENCODING_NV12:
      return AV_PIX_FMT_NV12;
    case MMAL_ENCODING_NV21:
      return AV_PIX_FMT_NV21;
    case MMAL_ENCODING_RGBA:
      return AV_PIX_FMT_RGBA;
    case MMAL_ENCODING_BGRA:
      return AV_PIX_FMT_BGRA;
    case MMAL_ENCODING_RGB32:
      return AV_PIX_FMT_RGB32;
    case MMAL_ENCODING_BGR32:
      return AV_PIX_FMT_BGR32;
    case MMAL_ENCODING_RGB24:
      return AV_PIX_FMT_RGB24;
    case MMAL_ENCODING_BGR24:
      return AV_PIX_FMT_BGR24;
    case MMAL_ENCODING_RGB16:
      return AV_PIX_FMT_RGB565;
    case MMAL_ENCODING_BGR16:
      return AV_PIX_FMT_BGR565;
    default:
      return AV_PIX_FMT_NONE;
  }
}

uint32_t CVideoBufferPoolMMAL::TranslateCodec(AVCodecID codec)
{
  switch (codec)
  {
    case AV_CODEC_ID_H264:
      return MMAL_ENCODING_H264;
    case AV_CODEC_ID_MPEG4:
      return MMAL_ENCODING_MP4V;
    case AV_CODEC_ID_MJPEG:
      return MMAL_ENCODING_MJPEG;
    case AV_CODEC_ID_H263:
      return MMAL_ENCODING_H263;
    case AV_CODEC_ID_MPEG1VIDEO:
      return MMAL_ENCODING_MP1V;
    case AV_CODEC_ID_MPEG2VIDEO:
      return MMAL_ENCODING_MP2V;
    case AV_CODEC_ID_VP6:
      return MMAL_ENCODING_VP6;
    case AV_CODEC_ID_VP7:
      return MMAL_ENCODING_VP7;
    case AV_CODEC_ID_VP8:
      return MMAL_ENCODING_VP8;
    case AV_CODEC_ID_WMV1:
      return MMAL_ENCODING_WMV1;
    case AV_CODEC_ID_WMV2:
      return MMAL_ENCODING_WMV2;
    case AV_CODEC_ID_WMV3:
      return MMAL_ENCODING_WMV3;
    case AV_CODEC_ID_VC1:
      return MMAL_ENCODING_WVC1;
    case AV_CODEC_ID_THEORA:
      return MMAL_ENCODING_THEORA;
    default:
      return MMAL_ENCODING_UNKNOWN;
  }
}

uint32_t CVideoBufferPoolMMAL::TranslateColorSpace(AVColorSpace space)
{
  switch (space)
  {
    case AVCOL_SPC_BT709:
      return MMAL_COLOR_SPACE_ITUR_BT709;
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT470BG:
      return MMAL_COLOR_SPACE_ITUR_BT601;
    case AVCOL_SPC_FCC:
      return MMAL_COLOR_SPACE_FCC;
    case AVCOL_SPC_SMPTE240M:
      return MMAL_COLOR_SPACE_SMPTE240M;
    default:
      return MMAL_COLOR_SPACE_UNKNOWN;
  }
}

MMALComponent CVideoBufferPoolMMAL::m_component{nullptr};

CVideoBufferPoolMMAL::CVideoBufferPoolMMAL()
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  if (m_component == nullptr)
  {
    MMALStatus status = MMAL_SUCCESS;
    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_NULL_SINK, &m_component);
    if (status == MMAL_SUCCESS)
    {
      if (m_component->is_enabled != 0)
        status = mmal_component_disable(m_component);
      if (m_component->input[0]->is_enabled != 0)
        status = mmal_port_disable(m_component->input[0]);
      if (status == MMAL_SUCCESS)
      {
        m_port = m_component->input[0];
        m_port->buffer_num = 0;
        m_port->buffer_size = 0;
        m_port->format->type = MMAL_ES_TYPE_VIDEO;
        m_port->format->encoding = MMAL_ENCODING_UNKNOWN;
        m_port->format->encoding_variant = MMAL_ENCODING_UNKNOWN;
        mmal_port_parameter_set_uint32(m_port, MMAL_PARAMETER_EXTRA_BUFFERS, 0);
        mmal_port_parameter_set_boolean(m_port, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
        if (mmal_port_format_commit(m_port) == MMAL_SUCCESS)
        {
        }
        else
          CLog::Log(LOGERROR, "CVideoBufferPoolMMAL::{} - failed to commit port", __FUNCTION__);
      }
      else
        CLog::Log(LOGERROR, "CVideoBufferPoolMMAL::{} - failed to disable ports", __FUNCTION__);
    }
    else
      CLog::Log(LOGERROR, "CVideoBufferPoolMMAL::{} - failed to create component", __FUNCTION__);
  }
  else
  {
    m_port = m_component->input[0];
    m_port->buffer_size = 0;
  }
}

CVideoBufferPoolMMAL::~CVideoBufferPoolMMAL()
{
  Release();

  std::unique_lock<CCriticalSection> lock(m_poolLock);
  for (auto buffer : m_all)
  {
    if (buffer)
    {
      buffer->Free();
      delete buffer;
    }
  }
  m_all.clear();

  if (m_port)
  {
    m_port = nullptr;
  }
}

void CVideoBufferPoolMMAL::Release()
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  CVideoBufferMMAL* buffer = nullptr;
  int i = 0;
  while (!m_free.empty())
  {
    i = m_free.front();
    m_free.pop_front();
    buffer = m_all[i];
    m_all[i] = nullptr;
    delete buffer;
  }

  while (!m_used.empty())
  {
    i = m_used.front();
    buffer = m_all[i];
    if (!buffer->IsRendering())
    {
      m_used.pop_front();
      m_all[i] = nullptr;
      buffer->Free();
    }
  }

  if (m_portFormat)
  {
    mmal_format_free(m_portFormat);
    m_portFormat = nullptr;
  }
}

CVideoBuffer* CVideoBufferPoolMMAL::Get()
{
  return Get(m_port->buffer_size);
}

CVideoBuffer* CVideoBufferPoolMMAL::Get(int size)
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  CVideoBufferMMAL* buffer = nullptr;
  int id = -1;
  if (!m_free.empty())
  {
    id = m_free.front();
    m_free.pop_front();
    buffer = m_all[id];
    if (!buffer->Realloc(size))
    {
      delete buffer;
      return nullptr;
    }
    m_used.push_back(id);
  }
  else
  {
    id = m_all.size();
    buffer = new CVideoBufferMMAL(m_port, id, m_format);
    if (!buffer->Alloc(size))
    {
      delete buffer;
      return nullptr;
    }
    m_all.push_back(buffer);
    m_used.push_back(id);
  }
  buffer->Acquire(GetPtr());
  return buffer;
}

void CVideoBufferPoolMMAL::Return(int id)
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  auto it = m_used.begin();
  while (it != m_used.end())
  {
    if (*it == id)
    {
      m_used.erase(it);
      break;
    }
    else
      ++it;
  }
  m_free.push_back(id);
}

void CVideoBufferPoolMMAL::Configure(AVPixelFormat format, int size)
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  if (!m_portFormat)
  {
    m_portFormat = mmal_format_alloc();
    m_portFormat->type = MMAL_ES_TYPE_VIDEO;
    m_portFormat->encoding = MMAL_ENCODING_UNKNOWN;
    m_portFormat->encoding_variant = MMAL_ENCODING_UNKNOWN;
    m_portFormat->extradata = nullptr;
    m_portFormat->extradata_size = 0;
    mmal_format_full_copy(m_portFormat, m_port->format);
  }

  m_format = format;
  m_portFormat->encoding = TranslateFormat(format);
  m_portFormat->encoding_variant = MMAL_ENCODING_UNKNOWN;
  m_port->buffer_size = size;
}

bool CVideoBufferPoolMMAL::IsConfigured()
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  return m_port && m_portFormat && m_portFormat->encoding != MMAL_ENCODING_UNKNOWN;
}

bool CVideoBufferPoolMMAL::IsCompatible(AVPixelFormat format, int size)
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  if (m_port && m_portFormat && m_portFormat->encoding == TranslateFormat(format) &&
      size == (int)m_port->buffer_size)
    return true;

  return false;
}

void CVideoBufferPoolMMAL::Released(CVideoBufferManager& videoBufferManager)
{
  //videoBufferManager.RegisterPool(std::make_shared<CVideoBufferPoolMMAL>());
}
