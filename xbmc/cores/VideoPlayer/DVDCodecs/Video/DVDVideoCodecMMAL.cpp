/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDVideoCodecMMAL.h"

#include "ServiceBroker.h"
#include "cores/VideoPlayer/DVDCodecs/DVDCodecs.h"
#include "cores/VideoPlayer/DVDCodecs/DVDFactoryCodec.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "settings/lib/Setting.h"
#include "threads/SingleLock.h"
#include "utils/CPUInfo.h"
#include "utils/StringUtils.h"
#include "utils/XTimeUtils.h"
#include "utils/log.h"
#include "windowing/dmx/WinSystemDmx.h"

#include <float.h>

#include <interface/mmal/mmal_events.h>
#include <interface/mmal/util/mmal_default_components.h>
#include <interface/mmal/util/mmal_util.h>
#include <interface/mmal/util/mmal_util_params.h>
#include <interface/mmal/vc/mmal_vc_msgs.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

using namespace MMAL;
using namespace std::chrono_literals;

constexpr const char* SETTING_VIDEOPLAYER_USEMMALDECODERFORHW{"videoplayer.usemmaldecoderforhw"};

std::unique_ptr<CDVDVideoCodec> CDVDVideoCodecMMAL::CreateCodec(CProcessInfo& processInfo)
{
  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
          CSettings::SETTING_VIDEOPLAYER_USEMMALDECODER))
    return std::make_unique<CDVDVideoCodecMMAL>(processInfo);
  return nullptr;
}

void CDVDVideoCodecMMAL::Register()
{
  auto settingsComponent = CServiceBroker::GetSettingsComponent();
  if (!settingsComponent)
    return;

  auto settings = settingsComponent->GetSettings();
  if (!settings)
    return;

  auto setting = settings->GetSetting(CSettings::SETTING_VIDEOPLAYER_USEMMALDECODER);
  if (!setting)
  {
    CLog::Log(LOGERROR, "Failed to load setting for: {}",
              CSettings::SETTING_VIDEOPLAYER_USEMMALDECODER);
    return;
  }
  setting->SetVisible(true);

  setting = settings->GetSetting(SETTING_VIDEOPLAYER_USEMMALDECODERFORHW);
  if (!setting)
  {
    CLog::Log(LOGERROR, "Failed to load setting for: {}", SETTING_VIDEOPLAYER_USEMMALDECODERFORHW);
    return;
  }
  setting->SetVisible(true);

  CDVDFactoryCodec::RegisterHWVideoCodec("mmal", CDVDVideoCodecMMAL::CreateCodec);
}

void CDVDVideoCodecMMAL::ProcessControlCallback(MMALPort port, MMALBufferHeader header)
{
  CDVDVideoCodecMMAL* codec = static_cast<CDVDVideoCodecMMAL*>((void*)port->userdata);
  if (codec && header->cmd == MMAL_EVENT_ERROR)
  {
    MMALStatus status = *(MMALStatus*)header->data;
    if (status != MMAL_EAGAIN)
    {
      codec->m_state = MCS_ERROR;
      CLog::Log(LOGWARNING, "CDVDVideoCodecMMAL::{} - decoder error reported: {}", __FUNCTION__,
                mmal_status_to_string(status));
    }
  }
  mmal_buffer_header_release(header);
}

void CDVDVideoCodecMMAL::ProcessInputCallback(MMALPort port, MMALBufferHeader header)
{
  mmal_buffer_header_release(header);
}

void CDVDVideoCodecMMAL::ProcessOutputCallback(MMALPort port, MMALBufferHeader header)
{
  CDVDVideoCodecMMAL* codec = static_cast<CDVDVideoCodecMMAL*>((void*)port->userdata);
  if (codec)
  {
    CVideoBufferMMAL* buffer = nullptr;
    if (header->cmd == MMAL_EVENT_FORMAT_CHANGED)
    {
      if (mmal_buffer_header_mem_lock(header) == MMAL_SUCCESS)
      {
        MMALFormatChangedEventArgs args = mmal_event_format_changed_get(header);
        if (mmal_format_full_copy(codec->m_output->format, args->format) == MMAL_SUCCESS)
        {
          std::unique_lock<CCriticalSection> lock(codec->m_recvLock);
          const MMAL_VIDEO_FORMAT_T* videoFormat = &codec->m_portFormat->es->video;
          if (videoFormat->crop.width > 0 && videoFormat->crop.height > 0)
          {
            codec->m_output->format->es->video.crop.width = videoFormat->crop.width;
            codec->m_output->format->es->video.crop.height = videoFormat->crop.height;
          }
          if (codec->m_output->format->es->video.frame_rate.num == 0 ||
              codec->m_output->format->es->video.frame_rate.den == 0)
          {
            codec->m_output->format->es->video.frame_rate.num = videoFormat->frame_rate.num;
            codec->m_output->format->es->video.frame_rate.den = videoFormat->frame_rate.den;
          }

          if (codec->m_hints.forced_aspect)
          {
            codec->m_output->format->es->video.par.num = videoFormat->par.num;
            codec->m_output->format->es->video.par.den = videoFormat->par.den;
          }

          codec->m_output->buffer_num = MMAL_CODEC_NUM_BUFFERS;
          codec->m_output->buffer_size = args->buffer_size_recommended;
          mmal_buffer_header_mem_unlock(header);
          lock.unlock();
          codec->m_bufferCondition.notifyAll();
        }
        else
        {
          mmal_buffer_header_mem_unlock(header);
          CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - failed to copy port format", __FUNCTION__);
        }
      }
      else
        CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - unable to lock memory", __FUNCTION__);
    }
    if ((header->flags & MMAL_BUFFER_HEADER_FLAG_ZEROCOPY) != 0 &&
        (buffer = static_cast<CVideoBufferMMAL*>(header->user_data)) != NULL)
    {
      MMALCodecState state = codec->m_state;
      if (header->cmd == 0 &&
          (state == MCS_DECODING || state == MCS_OPENED || state == MCS_FLUSHED ||
           state == MCS_FLUSHING || state == MCS_CLOSING))
      {
        if ((header->flags & MMAL_BUFFER_HEADER_FLAG_EOS) == 0)
        {
          std::unique_lock<CCriticalSection> lock(codec->m_recvLock);
          buffer->SetPortFormat(codec->m_output->format);
          codec->m_buffers.push_back(buffer);
          lock.unlock();
          codec->m_bufferCondition.notifyAll();
          //codec->m_ptsCurrent = buffer->GetHeader()->pts;
          //codec->m_bufferPool->Put(buffer);
        }
        else if (state == MCS_CLOSING)
        {
          buffer->Release();
          codec->Close();
        }
        else
          buffer->Release();
      }
      else
        buffer->Release();
    }
    else
      mmal_buffer_header_release(header);
  }
  else
    mmal_buffer_header_release(header);
}

CDVDVideoCodecMMAL::CDVDVideoCodecMMAL(CProcessInfo& processInfo)
  : CDVDVideoCodec(processInfo), CThread("NativeMMAL")
{
  MMALStatus status = MMAL_SUCCESS;
  m_name = "mmal";
  m_codecName = "    ";

  status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_DECODER, &m_component);
  if (status == MMAL_SUCCESS)
  {
    m_bufferPool = std::make_shared<CVideoBufferPoolMMAL>();
    if (m_component->is_enabled != 0)
      mmal_component_disable(m_component);

    int* priority = (int*)((uint8_t*)m_component->priv + 28);
    *priority = VCOS_THREAD_PRI_ABOVE_NORMAL;

    m_component->control->userdata = (MMALPortUserData)this;
    status = mmal_port_enable(m_component->control, CDVDVideoCodecMMAL::ProcessControlCallback);
    if (status == MMAL_SUCCESS)
    {
      MMALParameterHeader parameter = nullptr;
      m_input = m_component->input[0];
      m_output = m_component->output[0];
      m_input->userdata = (MMALPortUserData)this;
      m_output->userdata = (MMALPortUserData)this;
      m_portFormat = mmal_format_alloc();
      m_portFormat->extradata = nullptr;
      m_portFormat->extradata_size = 0;
      m_format = AV_PIX_FMT_NONE;

      mmal_port_parameter_set_boolean(m_input, MMAL_PARAMETER_VIDEO_DECODE_ERROR_CONCEALMENT,
                                      MMAL_TRUE);
      mmal_port_parameter_set_uint32(m_input, MMAL_PARAMETER_EXTRA_BUFFERS, 0);
      mmal_port_parameter_set_boolean(m_input, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
      mmal_port_parameter_set_boolean(m_input, MMAL_PARAMETER_NO_IMAGE_PADDING, MMAL_TRUE);
      mmal_port_parameter_set_boolean(m_input, MMAL_PARAMETER_VIDEO_TIMESTAMP_FIFO, MMAL_TRUE);

      mmal_port_parameter_set_uint32(m_output, MMAL_PARAMETER_EXTRA_BUFFERS, 0);
      mmal_port_parameter_set_boolean(m_output, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
      mmal_port_parameter_set_boolean(m_output, MMAL_PARAMETER_NO_IMAGE_PADDING, MMAL_TRUE);

      parameter =
          mmal_port_parameter_alloc_get(m_input, MMAL_PARAMETER_SUPPORTED_ENCODINGS, 0, &status);
      if (status == MMAL_SUCCESS)
      {
        uint32_t* codecs = (uint32_t*)((uint8_t*)parameter + sizeof(MMALParameterHeader));
        for (uint32_t i = 0; i < 16; i++)
        {
          if (i < (parameter->size - sizeof(MMALParameterHeader)) / 4)
            m_supportedCodecs[i] = codecs[i];
          else
            m_supportedCodecs[i] = MMAL_ENCODING_UNKNOWN;
        }
        mmal_port_parameter_free(parameter);
      }
      m_state = MCS_INITIALIZED;
      return;
    }
  }
  CLog::Log(LOGERROR, "Failed to create component");
  m_state = MCS_UNINITIALIZED;
}

CDVDVideoCodecMMAL::~CDVDVideoCodecMMAL()
{
  if (m_state != MCS_INITIALIZED)
  {
    if (m_state != MCS_CLOSED)
      Close(true);
  }
  m_bStop = true;
  if (!IsRunning() || !Join(500ms))
  {
  }

  if (m_input)
  {
    m_input->userdata = nullptr;
    std::unique_lock<CCriticalSection> lock(m_sendLock);
    if (m_inputPool && mmal_queue_length(m_inputPool->queue) >= m_inputPool->headers_num)
    {
      mmal_pool_destroy(m_inputPool);
      m_inputPool = nullptr;
    }
    m_input = nullptr;
  }

  if (m_output)
  {
    m_output->userdata = nullptr;
    m_output = nullptr;
  }

  if (m_component->control->is_enabled != 0)
  {
    if (mmal_port_disable(m_component->control) == MMAL_SUCCESS)
      m_component->control->userdata = nullptr;
    else
      CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - failed to disable control port", __FUNCTION__);
  }

  if (m_bufferPool)
  {
    std::static_pointer_cast<CVideoBufferPoolMMAL>(m_bufferPool)->Release();
    m_bufferPool = nullptr;
  }

  if (m_portFormat)
  {
    mmal_format_free(m_portFormat);
    m_portFormat = nullptr;
  }

  if (mmal_component_release(m_component) != MMAL_SUCCESS)
    CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - failed to release component", __FUNCTION__);

  m_component = nullptr;
  m_state = MCS_UNINITIALIZED;
}

void CDVDVideoCodecMMAL::UpdateProcessInfo()
{
  m_format = CVideoBufferPoolMMAL::TranslatePortFormat(m_portFormat->encoding);
  const char* pixFmtName = av_get_pix_fmt_name(m_format);
  mmal_4cc_to_string(&m_codecName[0], m_codecName.size(), m_portFormat->encoding);
  m_name = StringUtils::TrimRight(m_codecName) + std::string("-mmal");
  StringUtils::ToLower(m_name);

  m_fps = 0.0f;
  m_aspect = 0.0f;
  if (m_portFormat->es->video.frame_rate.num > 0 && m_portFormat->es->video.frame_rate.den > 0)
    m_fps = (float)m_portFormat->es->video.frame_rate.num /
            (float)m_portFormat->es->video.frame_rate.den;

  if (m_portFormat->es->video.par.num > 0 && m_portFormat->es->video.par.den > 0)
    m_aspect = (float)m_portFormat->es->video.par.num / (float)m_portFormat->es->video.par.den;

  if (m_portFormat->es->video.crop.width > 0 && m_portFormat->es->video.crop.height > 0)
  {
    m_width = m_portFormat->es->video.crop.width;
    m_height = m_portFormat->es->video.crop.height;
  }
  else
  {
    m_width = m_portFormat->es->video.width;
    m_height = m_portFormat->es->video.height;
  }

  if (m_aspect > 0.0f)
  {
    m_displayWidth = (static_cast<uint32_t>(lrint(m_height * m_aspect))) & -3;
    m_displayHeight = m_height;
    if (m_displayWidth > m_width)
    {
      m_displayWidth = m_width;
      m_displayHeight = (static_cast<uint32_t>(lrint(m_width / m_aspect))) & -3;
    }
  }
  else
  {
    m_displayWidth = m_width;
    m_displayHeight = m_height;
  }

  m_bufferPool->Configure(m_format, m_output->buffer_size);

  std::list<EINTERLACEMETHOD> intMethods;
  intMethods.push_back(VS_INTERLACEMETHOD_NONE);
  m_processInfo.UpdateDeinterlacingMethods(intMethods);

  m_processInfo.SetVideoPixelFormat(pixFmtName ? pixFmtName : "");
  m_processInfo.SetVideoDimensions(m_width, m_height);
  m_processInfo.SetVideoDecoderName(m_name, true);
  m_processInfo.SetVideoDeintMethod("none");
  m_processInfo.SetVideoStereoMode("mono");
  m_processInfo.SetVideoDAR(m_aspect);
  m_processInfo.SetVideoFps(m_fps);

  m_state = MCS_DECODING;
}

bool CDVDVideoCodecMMAL::Open(CDVDStreamInfo& hints, CDVDCodecOptions& options)
{
  if (m_state != MCS_INITIALIZED)
    return false;

  uint32_t encoding = CVideoBufferPoolMMAL::TranslateCodec(hints.codec);

  if (encoding != MMAL_ENCODING_UNKNOWN)
  {
    uint32_t codec = MMAL_ENCODING_UNKNOWN;
    for (int i = 0; i < 15; i++)
    {
      if (m_supportedCodecs[i] == encoding)
      {
        codec = encoding;
        break;
      }
    }
    encoding = codec;
  }

  if (encoding == MMAL_ENCODING_UNKNOWN)
  {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecMMAL::{} - unsupported codec", __FUNCTION__);
    return false;
  }

  bool configureCodec = true;
  MMALStatus status = MMAL_SUCCESS;

  m_input->format->type = MMAL_ES_TYPE_VIDEO;
  m_input->format->flags = MMAL_ES_FORMAT_FLAG_FRAMED;
  m_input->format->encoding = encoding;
  m_input->format->es->video.width = hints.width;
  m_input->format->es->video.height = hints.height;
  m_input->format->es->video.frame_rate.num = hints.fpsrate;
  m_input->format->es->video.frame_rate.den = hints.fpsscale;
  m_input->format->es->video.par.num = 1;
  m_input->format->es->video.par.den = 1;

  if (hints.aspect > 0)
  {
    double delta = DBL_MAX;
    int w = 1, h = 1;
    for (int i = 0; i < 127; i++)
    {
      double d = (double)w / (double)h - hints.aspect;
      if (d < 0)
        d = abs(((double)++w / (double)h) - hints.aspect);
      else
        d = abs(((double)w / (double)++h) - hints.aspect);
      if (d < delta)
      {
        delta = d;
        m_input->format->es->video.par.num = w;
        m_input->format->es->video.par.den = h;
      }
    }
  }

  if (configureCodec && hints.extradata && hints.extrasize > 0)
  {
    if (hints.extrasize < MMAL_FORMAT_EXTRADATA_MAX_SIZE &&
        mmal_format_extradata_alloc(m_input->format, hints.extrasize) == MMAL_SUCCESS)
    {
      m_input->format->extradata_size = hints.extrasize;
      memcpy(m_input->format->extradata, hints.extradata, hints.extrasize);
      configureCodec = false;
    }
  }

  mmal_port_parameter_set_boolean(m_input, MMAL_PARAMETER_VIDEO_INTERPOLATE_TIMESTAMPS,
                                  hints.ptsinvalid ? MMAL_TRUE : MMAL_FALSE);

  mmal_port_parameter_set_uint32(m_input, MMAL_PARAMETER_VIDEO_MAX_NUM_CALLBACKS,
                                 -10); //DPB=3+9, 12 total

  if (mmal_port_format_commit(m_input) != MMAL_SUCCESS)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - failed to commit codec format", __FUNCTION__);
    return false;
  }

  m_input->buffer_num = 24;
  m_input->buffer_size = 4096 * 24; //1024 * 64;

  if (m_input->buffer_alignment_min > 0)
    m_input->buffer_size = VCOS_ALIGN_UP(m_input->buffer_size, m_input->buffer_alignment_min);

  m_inputPool = mmal_port_pool_create(m_input, m_input->buffer_num, m_input->buffer_size);

  if (!m_inputPool)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - failed to create codec buffer pool",
              __FUNCTION__);
    return false;
  }

  m_output->format->type = MMAL_ES_TYPE_VIDEO;
  m_output->format->flags = MMAL_ES_FORMAT_FLAG_FRAMED;
  m_output->format->encoding = MMAL_ENCODING_UNKNOWN;
  m_output->format->encoding_variant = MMAL_ENCODING_UNKNOWN;
  m_output->format->es->video.width = m_input->format->es->video.width;
  m_output->format->es->video.height = m_input->format->es->video.height;
  m_output->format->es->video.frame_rate.num = m_input->format->es->video.frame_rate.num;
  m_output->format->es->video.frame_rate.den = m_input->format->es->video.frame_rate.den;
  m_output->format->es->video.color_space = MMAL_COLOR_SPACE_UNKNOWN;

  if (hints.forced_aspect)
  {
    m_output->format->es->video.par.num = m_input->format->es->video.par.num;
    m_output->format->es->video.par.den = m_input->format->es->video.par.den;
  }

  if (m_input->format->es->video.width < m_output->format->es->video.width)
    m_output->format->es->video.crop.width = m_input->format->es->video.width;
  if (m_input->format->es->video.height < m_output->format->es->video.height)
    m_output->format->es->video.crop.height = m_input->format->es->video.height;

  if (mmal_port_format_commit(m_output) != MMAL_SUCCESS)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - failed to commit port format", __FUNCTION__);
    return false;
  }

  if (mmal_format_full_copy(m_portFormat, m_output->format) != MMAL_SUCCESS)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - failed to copy port format", __FUNCTION__);
    return false;
  }

  m_output->buffer_num = MMAL_CODEC_NUM_BUFFERS;
  m_output->buffer_size = m_output->buffer_size_recommended;

  if (mmal_port_enable(m_input, CDVDVideoCodecMMAL::ProcessInputCallback) != MMAL_SUCCESS)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - failed to enable input port", __FUNCTION__);
    return false;
  }

  if (mmal_port_enable(m_output, CDVDVideoCodecMMAL::ProcessOutputCallback) != MMAL_SUCCESS)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - failed to enable output port", __FUNCTION__);
    return false;
  }

  if (m_component->is_enabled == 0)
  {
    if (mmal_component_enable(m_component) != MMAL_SUCCESS)
    {
      CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - failed to enable component", __FUNCTION__);
      return false;
    }
  }

  if (configureCodec)
  {
    if (!ConfigureCodec((uint8_t*)hints.extradata, hints.extrasize))
      return false;
  }

  m_hints = hints;
  m_state = MCS_OPENED;
  if (!IsRunning())
    Create(false);

  return true;
}

bool CDVDVideoCodecMMAL::ConfigureCodec(uint8_t* extraData, uint32_t extraSize)
{
  uint32_t size = extraSize;
  uint8_t* data = extraData;

  std::unique_lock<CCriticalSection> lock(m_sendLock);
  MMALBufferHeader header = nullptr;
  while (size > 0 && (header = mmal_queue_get(m_inputPool->queue)) != NULL)
  {
    mmal_buffer_header_reset(header);
    header->cmd = 0;
    header->flags = MMAL_BUFFER_HEADER_FLAG_CONFIG;

    if (size > header->alloc_size)
      header->length = header->alloc_size;
    else
      header->length = size;

    if (data)
    {
      mmal_buffer_header_mem_lock(header);
      memcpy(header->data, data, header->length);
      mmal_buffer_header_mem_unlock(header);
    }

    size -= header->length;
    data += header->length;

    if (size == 0)
      header->flags |= MMAL_BUFFER_HEADER_FLAG_FRAME_END;

    if (mmal_port_send_buffer(m_input, header) != MMAL_SUCCESS)
    {
      m_state = MCS_ERROR;
      CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - failed to configure codec", __FUNCTION__);
      return false;
    }
  }
  return true;
}

bool CDVDVideoCodecMMAL::SendEndOfStream()
{
  MMALCodecState state = m_state;

  if (state == MCS_DECODING)
  {
    std::unique_lock<CCriticalSection> lock(m_sendLock);
    MMALBufferHeader header = mmal_queue_get(m_inputPool->queue);
    mmal_buffer_header_reset(header);
    header->cmd = 0;
    header->flags = MMAL_BUFFER_HEADER_FLAG_EOS;
    header->length = 0;
    m_state = MCS_CLOSING;
    if (mmal_port_send_buffer(m_input, header) != MMAL_SUCCESS)
    {
      m_state = state;
      CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - unable to send eos signal", __FUNCTION__);
      return false;
    }
  }
  return true;
}

bool CDVDVideoCodecMMAL::AddData(const DemuxPacket& packet)
{
  uint32_t size = (uint32_t)packet.iSize;
  uint8_t* data = packet.pData;
  MMALCodecState state = m_state;

  if (state == MCS_FLUSHING || state == MCS_ERROR)
    return false;
  else if (state == MCS_CLOSING || state == MCS_CLOSED)
    return true;
  else if (data == nullptr || size == 0)
    return SendEndOfStream();

  uint32_t freeBytes = m_input->buffer_size * (mmal_queue_length(m_inputPool->queue) - 1);
  if (size > freeBytes)
  {
    m_rejectedSize = size;
    return false;
  }
  else
    m_rejectedSize = 0;

  std::unique_lock<CCriticalSection> lock(m_sendLock);
  int64_t ptsPacket = MMAL_TIME_UNKNOWN;
  int64_t dtsPacket = MMAL_TIME_UNKNOWN;

  if (packet.dts != DVD_NOPTS_VALUE)
    dtsPacket = static_cast<int64_t>(packet.dts / DVD_TIME_BASE * AV_TIME_BASE);

  if (!m_hints.ptsinvalid && packet.pts != DVD_NOPTS_VALUE)
    ptsPacket = static_cast<int64_t>(packet.pts / DVD_TIME_BASE * AV_TIME_BASE);

  MMALStatus status = MMAL_SUCCESS;
  MMALBufferHeader header = nullptr;
  while (size > 0 && (header = mmal_queue_get(m_inputPool->queue)) != NULL)
  {
    mmal_buffer_header_reset(header);
    header->cmd = 0;
    header->flags = MMAL_BUFFER_HEADER_FLAG_ZEROCOPY;

    header->pts = ptsPacket;
    header->dts = dtsPacket;

    if (size == (uint32_t)packet.iSize)
    {
      if (state == MCS_FLUSHED)
      {
        m_state = MCS_DECODING;
        header->flags |= MMAL_BUFFER_HEADER_FLAG_DISCONTINUITY;
      }
      else if ((m_codecControlFlags & DVD_CODEC_CTRL_DROP) != 0)
      {
        header->flags |= MMAL_BUFFER_HEADER_FLAG_DECODEONLY;
        m_dropped = true;
        m_droppedFrames++;
      }
      else if (m_dropped)
      {
        header->flags |= MMAL_BUFFER_HEADER_FLAG_SEEK;
        m_dropped = false;
      }
      header->flags |= MMAL_BUFFER_HEADER_FLAG_FRAME_START;
    }

    if (size > header->alloc_size)
      header->length = header->alloc_size;
    else
      header->length = size;

    if (data)
    {
      if (mmal_buffer_header_mem_lock(header) == MMAL_SUCCESS)
      {
        memcpy(header->data, data, header->length);
        mmal_buffer_header_mem_unlock(header);
      }
      else
      {
        CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - unable to lock memory", __FUNCTION__);
        m_state = MCS_RESET;
        return false;
      }
    }

    size -= header->length;
    data += header->length;

    if (size == 0)
      header->flags |= MMAL_BUFFER_HEADER_FLAG_FRAME_END;

    status = mmal_port_send_buffer(m_input, header);

    if (status == MMAL_EAGAIN)
      status = mmal_port_send_buffer(m_input, header);

    if (status != MMAL_SUCCESS)
    {
      m_state = MCS_RESET;
      CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - unable to send buffer to input port",
                __FUNCTION__);
      return false;
    }
  }
  if (size == 0)
    return true;

  m_state = MCS_RESET;
  CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - unable to send complete frame", __FUNCTION__);
  return false;
}

CDVDVideoCodec::VCReturn CDVDVideoCodecMMAL::GetPicture(VideoPicture* pVideoPicture)
{
  CDVDVideoCodec::VCReturn result = VC_NONE;
  MMALCodecState state = m_state;

  if (state == MCS_INITIALIZED || state == MCS_UNINITIALIZED)
  {
    result = VC_ERROR;
  }
  else if (state == MCS_CLOSED || state == MCS_ERROR)
  {
    result = VC_EOF;
  }
  else if (state == MCS_OPENED)
  {
    result = VC_BUFFER;
  }
  else if (state == MCS_RESET)
  {
    result = VC_FLUSHED;
  }
  else
  {
    std::unique_lock<CCriticalSection> lock(m_recvLock);
    uint32_t rendered = m_buffers.size();
    uint32_t inputFree = mmal_queue_length(m_inputPool->queue);
    uint32_t renderLimit = (static_cast<float>(inputFree - 1) / (m_inputPool->headers_num - 1)) *
                           MMAL_CODEC_NUM_BUFFERS;
    bool receive = rendered <= MMAL_CODEC_NUM_BUFFERS && inputFree > 1 &&
                   (m_input->buffer_size * (inputFree - 1)) > m_rejectedSize;
    bool drain = state == MCS_CLOSING || state == MCS_FLUSHING || state == MCS_FLUSHED ||
                 (m_codecControlFlags & DVD_CODEC_CTRL_DRAIN) != 0 ||
                 (m_codecControlFlags & DVD_CODEC_CTRL_DROP_ANY) != 0;

    if (rendered > 0 && (drain || rendered >= renderLimit))
    {
      CVideoBufferMMAL* buffer = m_buffers.front();
      bool drop = (m_codecControlFlags & DVD_CODEC_CTRL_DROP) != 0;

      pVideoPicture->Reset();
      pVideoPicture->iFlags |= m_codecControlFlags;

      if ((pVideoPicture->iFlags & DVD_CODEC_CTRL_DRAIN) != 0)
        pVideoPicture->iFlags &= ~DVD_CODEC_CTRL_DRAIN;
      pVideoPicture->hasDisplayMetadata = false;
      pVideoPicture->hasLightMetadata = false;

      pVideoPicture->pixelFormat = m_format;

      pVideoPicture->iWidth = m_width;
      pVideoPicture->iHeight = m_height;
      pVideoPicture->iDisplayWidth = m_displayWidth;
      pVideoPicture->iDisplayHeight = m_displayHeight;

      pVideoPicture->color_range = m_hints.colorRange == AVCOL_RANGE_JPEG;
      pVideoPicture->color_primaries = m_hints.colorPrimaries;
      pVideoPicture->color_transfer = m_hints.colorTransferCharacteristic;
      pVideoPicture->color_space = m_hints.colorSpace;
      pVideoPicture->colorBits = m_hints.bitsperpixel;

      if (m_hints.masteringMetadata)
      {
        pVideoPicture->displayMetadata = *m_hints.masteringMetadata.get();
        pVideoPicture->hasDisplayMetadata = true;
      }

      if (m_hints.contentLightMetadata)
      {
        pVideoPicture->lightMetadata = *m_hints.contentLightMetadata.get();
        pVideoPicture->hasLightMetadata = true;
      }

      if (drop && (pVideoPicture->iFlags & DVP_FLAG_DROPPED) == 0)
        pVideoPicture->iFlags |= DVP_FLAG_DROPPED;

      buffer->WritePicture(pVideoPicture);

      pVideoPicture->videoBuffer = buffer;
      m_buffers.pop_front();
      result = VC_PICTURE;
    }
    else if (state != MCS_CLOSING && receive)
    {
      if ((m_codecControlFlags & DVD_CODEC_CTRL_DRAIN) != 0)
        m_codecControlFlags &= ~DVD_CODEC_CTRL_DRAIN;
      result = VC_BUFFER;
    }
    else if (state == MCS_CLOSING && inputFree >= m_inputPool->headers_num)
    {
      result = VC_EOF;
      m_state = MCS_CLOSED;
    }
  }
  return result;
}

bool CDVDVideoCodecMMAL::Close(bool force)
{
  MMALCodecState state = m_state;
  if (state == MCS_CLOSING || force)
  {
    m_state = MCS_CLOSED;
    if (m_input->is_enabled != 0)
    {
      std::unique_lock<CCriticalSection> lock(m_portLock);
      if (mmal_port_disable(m_input) == MMAL_SUCCESS)
        m_input->userdata = nullptr;
      else
        CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - unable to disable input port", __FUNCTION__);
    }

    {
      std::unique_lock<CCriticalSection> lock(m_recvLock);
      while (!m_buffers.empty())
      {
        CVideoBufferMMAL* buffer = m_buffers.front();
        m_buffers.pop_front();
        buffer->Release();
      }
    }

    if (m_output->is_enabled != 0)
    {
      std::unique_lock<CCriticalSection> lock(m_portLock);
      if (mmal_port_disable(m_output) == MMAL_SUCCESS)
        m_output->userdata = nullptr;
      else
        CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - unable to disable output port", __FUNCTION__);
    }

    m_ptsCurrent = MMAL_TIME_UNKNOWN;
    m_droppedFrames = -1;

    if (m_component->is_enabled != 0)
    {
      if (mmal_component_disable(m_component) != MMAL_SUCCESS)
        CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - unable to disable component", __FUNCTION__);
    }
  }
  return true;
}

void CDVDVideoCodecMMAL::Reset()
{
  MMALCodecState state = m_state;
  if (state == MCS_DECODING)
  {
    m_state = MCS_FLUSHING;
    {
      std::unique_lock<CCriticalSection> lock(m_sendLock);
      if (mmal_port_flush(m_input) != MMAL_SUCCESS)
        CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - unable to flush input port", __FUNCTION__);
    }
    {
      std::unique_lock<CCriticalSection> lock(m_recvLock);
      while (!m_buffers.empty())
      {
        CVideoBufferMMAL* buffer = m_buffers.front();
        m_buffers.pop_front();
        buffer->Release();
      }
      if (mmal_port_flush(m_output) != MMAL_SUCCESS)
        CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - unable to flush output port", __FUNCTION__);

      m_state = MCS_FLUSHED;
    }
    m_ptsCurrent = MMAL_TIME_UNKNOWN;
    m_droppedFrames = -1;
    m_dropped = false;
    if ((m_codecControlFlags & DVD_CODEC_CTRL_DRAIN) != 0)
      m_codecControlFlags &= ~DVD_CODEC_CTRL_DRAIN;
  }
}

void CDVDVideoCodecMMAL::SetCodecControl(int flags)
{
  m_codecControlFlags = flags;
}

void CDVDVideoCodecMMAL::SetSpeed(int iSpeed)
{
  m_playbackSpeed = iSpeed;
}

bool CDVDVideoCodecMMAL::GetCodecStats(double& pts, int& droppedFrames, int& skippedPics)
{
  if (m_ptsCurrent != MMAL_TIME_UNKNOWN)
  {
    pts = static_cast<double>(m_ptsCurrent) * DVD_TIME_BASE / AV_TIME_BASE;
  }

  if (m_droppedFrames != -1)
    droppedFrames = m_droppedFrames + 1;
  else
    droppedFrames = -1;
  m_droppedFrames = -1;
  skippedPics = -1;
  return true;
}

void CDVDVideoCodecMMAL::Process()
{
  MMALCodecState state = m_state;
  CVideoBufferMMAL* buffer = nullptr;
  int rendered = 0;
  while (!m_bStop)
  {
    if (state == MCS_DECODING)
    {
      std::unique_lock<CCriticalSection> lock(m_recvLock);
      rendered = m_buffers.size();

      if (rendered <= MMAL_CODEC_NUM_BUFFERS)
      {
        if ((buffer = dynamic_cast<CVideoBufferMMAL*>(m_bufferPool->Get())) != NULL)
        {
          if (mmal_port_send_buffer(m_output, buffer->GetHeader()) != MMAL_SUCCESS)
          {
            buffer->Release();
            buffer = nullptr;
          }
        }
      }
      if (!buffer)
        m_bufferCondition.wait(lock, 40ms);
    }
    else if (state == MCS_OPENED)
    {
      std::unique_lock<CCriticalSection> lock(m_recvLock);

      if (m_bufferCondition.wait(lock, 10s) && m_bufferPool->IsConfigured() == false)
      {
        if (m_output->format->es->video.color_space == MMAL_COLOR_SPACE_UNKNOWN)
          m_output->format->es->video.color_space =
              CVideoBufferPoolMMAL::TranslateColorSpace(m_hints.colorSpace);
        std::unique_lock<CCriticalSection> lock(m_portLock);
        if (mmal_port_format_commit(m_output) == MMAL_SUCCESS)
        {
          if (mmal_format_full_copy(m_portFormat, m_output->format) == MMAL_SUCCESS)
            UpdateProcessInfo();
        }
        else
          CLog::Log(LOGERROR, "CDVDVideoCodecMMAL::{} - failed to commit port format",
                    __FUNCTION__);
      }
    }
    else
      KODI::TIME::Sleep(40ms);
    state = m_state;
  }
}
