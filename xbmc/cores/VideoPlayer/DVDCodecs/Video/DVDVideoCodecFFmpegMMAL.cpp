/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDVideoCodecFFmpegMMAL.h"

#include "ServiceBroker.h"
#include "cores/VideoPlayer/DVDCodecs/DVDCodecs.h"
#include "cores/VideoPlayer/DVDCodecs/DVDFactoryCodec.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "settings/lib/Setting.h"
#include "threads/SingleLock.h"
#include "utils/CPUInfo.h"
#include "utils/XTimeUtils.h"
#include "utils/log.h"

#include <float.h>
#include <signal.h>
#include <thread>

#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}
using namespace MMAL;
using namespace std::chrono_literals;

enum AVStatus : int
{
  AV_SUCCESS = 0, /**< Success */
  AV_ENOMEM = AVERROR(ENOMEM), /**< Out of memory */
  AV_ENOSPC = AVERROR(ENOSPC), /**< Out of resources (other than memory) */
  AV_EINVAL = AVERROR(EINVAL), /**< Argument is invalid */
  AV_ENOSYS = AVERROR(ENOSYS), /**< Function not implemented */
  AV_ENOENT = AVERROR(ENOENT), /**< No such file or directory */
  AV_ENXIO = AVERROR(ENXIO), /**< No such device or address */
  AV_EIO = AVERROR(EIO), /**< I/O error */
  AV_ESPIPE = AVERROR(ESPIPE), /**< Illegal seek */
  AV_ECORRUPT = AVERROR_INVALIDDATA, /**< Invalid data */
  AV_EOS = AVERROR_EOF, /**< End of stream */
  AV_EAGAIN = AVERROR(EAGAIN), /**< Resource temporarily unavailable. Try again later*/
  AV_EFAULT = AVERROR(ENOENT), /**< Bad address */
  AV_STATUS_MAX = 0x7FFFFFFF /**< Force to 32 bit */
};

constexpr const char* SETTING_VIDEOPLAYER_USEMMALDECODERFORHW{"videoplayer.usemmaldecoderforhw"};

std::unique_ptr<CDVDVideoCodec> CDVDVideoCodecFFmpegMMAL::CreateCodec(CProcessInfo& processInfo)
{
  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
          CSettings::SETTING_VIDEOPLAYER_USEMMALDECODER))
    return std::make_unique<CDVDVideoCodecFFmpegMMAL>(processInfo);
  return nullptr;
}

void CDVDVideoCodecFFmpegMMAL::Register()
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

  CDVDFactoryCodec::RegisterHWVideoCodec("mmal-hevc", CDVDVideoCodecFFmpegMMAL::CreateCodec);
}

enum AVPixelFormat CDVDVideoCodecFFmpegMMAL::GetFormatCallback(struct AVCodecContext* avctx,
                                                               const enum AVPixelFormat* fmt)
{
  CDVDVideoCodecFFmpegMMAL* codec = static_cast<CDVDVideoCodecFFmpegMMAL*>(avctx->opaque);
  if (codec)
  {
    for (int n = 0; fmt[n] != AV_PIX_FMT_NONE; n++)
    {
      if (fmt[n] == AV_PIX_FMT_SAND128 || fmt[n] == AV_PIX_FMT_SAND64_10 ||
          fmt[n] == AV_PIX_FMT_YUV420P || fmt[n] == AV_PIX_FMT_YUV420P10 ||
          fmt[n] == AV_PIX_FMT_YUV422P || fmt[n] == AV_PIX_FMT_YUV422P10)
      {
        if (avctx->colorspace != AVCOL_SPC_UNSPECIFIED)
          codec->m_hints.colorSpace = avctx->colorspace;

        codec->m_portFormat->encoding = CVideoBufferPoolMMAL::TranslateFormat(fmt[n]);
        codec->m_portFormat->encoding_variant = 0;
        codec->m_portFormat->es->video.width = avctx->width;
        codec->m_portFormat->es->video.height = avctx->height;
        codec->m_portFormat->es->video.color_space =
            CVideoBufferPoolMMAL::TranslateColorSpace(codec->m_hints.colorSpace);

        if (fmt[n] == AV_PIX_FMT_SAND128 || fmt[n] == AV_PIX_FMT_SAND64_10)
        {
          AVMMALZcFrameGeometry geometry =
              av_mmal_zc_frame_geometry(fmt[n], avctx->width, avctx->height);
          if (geometry.stripe_is_yc)
            codec->m_portFormat->es->video.width = geometry.height_y + geometry.height_c;
          else
            codec->m_portFormat->es->video.width = geometry.height_y;
          codec->m_portFormat->es->video.height = geometry.height_y;

          codec->m_portFormat->es->video.crop.width = avctx->width;
          codec->m_portFormat->es->video.crop.height = avctx->height;
          codec->m_portFormat->flags |= MMAL_ES_FORMAT_FLAG_COL_FMTS_WIDTH_IS_COL_STRIDE;
        }
        else
        {
          codec->m_portFormat->es->video.width = VCOS_ALIGN_UP(avctx->width, 32);
          codec->m_portFormat->es->video.height = VCOS_ALIGN_UP(avctx->height, 16);

          if ((uint32_t)avctx->width < codec->m_portFormat->es->video.width)
            codec->m_portFormat->es->video.crop.width = avctx->width;

          if ((uint32_t)avctx->height < codec->m_portFormat->es->video.height)
            codec->m_portFormat->es->video.crop.height = avctx->height;
        }
        if (avctx->framerate.num != 0 && avctx->framerate.den != 1)
        {
          codec->m_portFormat->es->video.frame_rate.num = avctx->framerate.num;
          codec->m_portFormat->es->video.frame_rate.den = avctx->framerate.den;
        }

        if (codec->m_hints.forced_aspect)
        {
          codec->m_portFormat->es->video.par.num = avctx->sample_aspect_ratio.num;
          codec->m_portFormat->es->video.par.den = avctx->sample_aspect_ratio.den;
        }
        codec->UpdateProcessInfo();
        return fmt[n];
      }
    }
  }

  CLog::Log(LOGERROR, "CDVDVideoCodecFFmpegMMAL::{} - unsupported pixel format", __FUNCTION__);
  return AV_PIX_FMT_NONE;
}

CDVDVideoCodecFFmpegMMAL::CDVDVideoCodecFFmpegMMAL(CProcessInfo& processInfo)
  : CDVDVideoCodec(processInfo), CThread("FFmpegMMAL")
{
  m_name = "ff-mmal";

  m_bufferPool = std::make_shared<CVideoBufferPoolMMAL>();
  m_portFormat = mmal_format_alloc();
  m_portFormat->extradata = nullptr;
  m_portFormat->extradata_size = 0;

  m_state = MCS_INITIALIZED;
}

CDVDVideoCodecFFmpegMMAL::~CDVDVideoCodecFFmpegMMAL()
{
  if (m_state == MCS_DECODING)
  {
    Flush();
    KODI::TIME::Sleep(250ms);
  }
  m_state = MCS_CLOSED;
  m_bStop = true;
  if (!IsRunning() || !Join(500ms))
  {
    if (m_context)
    {
      avcodec_free_context(&m_context);
      m_context = nullptr;
    }
  }

  if (m_bufferPool)
  {
    m_bufferPool->Dispose();
    m_bufferPool = nullptr;
  }

  if (m_portFormat)
  {
    mmal_format_free(m_portFormat);
    m_portFormat = nullptr;
  }
  m_state = MCS_UNINITIALIZED;
}

void CDVDVideoCodecFFmpegMMAL::UpdateProcessInfo()
{
  m_format = CVideoBufferPoolMMAL::TranslatePortFormat(m_portFormat->encoding);
  const char* pixFmtName = av_get_pix_fmt_name(m_format);

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

  VideoPicture picture{};

  picture.Reset();

  picture.hasDisplayMetadata = false;
  picture.hasLightMetadata = false;

  picture.pixelFormat = m_format;

  picture.iWidth = m_width;
  picture.iHeight = m_height;
  picture.iDisplayWidth = m_displayWidth;
  picture.iDisplayHeight = m_displayHeight;

  picture.color_range = m_hints.colorRange == AVCOL_RANGE_JPEG;
  picture.color_primaries = m_hints.colorPrimaries;
  picture.color_transfer = m_hints.colorTransferCharacteristic;
  picture.color_space = m_hints.colorSpace;
  picture.colorBits = m_hints.bitsperpixel;

  if (m_hints.masteringMetadata)
  {
    picture.displayMetadata = *m_hints.masteringMetadata.get();
    picture.hasDisplayMetadata = true;
  }

  if (m_hints.contentLightMetadata)
  {
    picture.lightMetadata = *m_hints.contentLightMetadata.get();
    picture.hasLightMetadata = true;
  }
  int size = 0;
  if (m_codec->id != AV_CODEC_ID_HEVC)
    size = av_image_get_buffer_size(m_format, VCOS_ALIGN_UP(m_width, 32),
                                    VCOS_ALIGN_UP(m_height, 16), 1);

  m_bufferPool->Configure(m_portFormat, &picture, MMAL_FFMPEG_CODEC_NUM_BUFFERS + 1, size);
  m_processInfo.SetVideoPixelFormat(pixFmtName ? pixFmtName : "");
  m_processInfo.SetVideoDimensions(m_width, m_height);
  m_processInfo.SetVideoDecoderName(m_name, true);
  m_processInfo.SetVideoDeintMethod("none");
  m_processInfo.SetVideoStereoMode("mono");
  m_processInfo.SetVideoDAR(m_aspect);
  m_processInfo.SetVideoFps(m_fps);

  m_state = MCS_DECODING;
  m_ptsCurrent = AV_NOPTS_VALUE;
  m_receive = true;
  if (!IsRunning())
  {
    Create(false);
  }
}

bool CDVDVideoCodecFFmpegMMAL::Open(CDVDStreamInfo& hints, CDVDCodecOptions& options)
{
  if (m_state != MCS_INITIALIZED)
    return false;

  if (hints.codec == AV_CODEC_ID_HEVC)
    m_codec = avcodec_find_decoder_by_name("hevc_mmal");
  else if (hints.codec == AV_CODEC_ID_AV1)
    m_codec = avcodec_find_decoder_by_name("libdav1d");
  else if (hints.codec == AV_CODEC_ID_VP9)
    m_codec = avcodec_find_decoder_by_name("vp9");
  else
  {
    m_codec = avcodec_find_decoder(hints.codec);
    if (!m_codec)
    {
      CLog::Log(LOGDEBUG, "CDVDVideoCodecFFmpegMMAL::{} - unsupported codec", __FUNCTION__);
      return false;
    }
  }

  if (!m_codec)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecFFmpegMMAL::{} - failed to create codec", __FUNCTION__);
    return false;
  }

  if ((m_context = avcodec_alloc_context3(m_codec)) == NULL)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecFFmpegMMAL::{} - failed to create context", __FUNCTION__);
    return false;
  }
  AVDictionary* codecOptions = NULL;
  m_context->opaque = static_cast<void*>(this);
  m_context->get_format = CDVDVideoCodecFFmpegMMAL::GetFormatCallback;
  m_context->debug_mv = 0;
  m_context->debug = 0;
  m_context->workaround_bugs = FF_BUG_AUTODETECT;
  m_context->thread_safe_callbacks = 1;
  if (m_codec->id == AV_CODEC_ID_HEVC)
  {
    m_context->thread_count = CServiceBroker::GetCPUInfo()->GetCPUCount() * 2;
    m_context->thread_type = FF_THREAD_FRAME;
  }
  else if (m_codec->id == AV_CODEC_ID_AV1)
  {
    m_context->thread_count = CServiceBroker::GetCPUInfo()->GetCPUCount();
    av_dict_set_int(&codecOptions, "framethreads", m_context->thread_count * 2, 0);
    av_dict_set_int(&codecOptions, "tilethreads", m_context->thread_count, 0);
  }
  else if (m_codec->id == AV_CODEC_ID_VP9)
  {
    m_context->thread_count = CServiceBroker::GetCPUInfo()->GetCPUCount() * 3 / 2;
    m_context->thread_type = FF_THREAD_FRAME;
  }
  else if ((m_codec->capabilities & AV_CODEC_CAP_AUTO_THREADS) == 0)
  {
    m_context->thread_count = CServiceBroker::GetCPUInfo()->GetCPUCount() * 3 / 2;
  }

  m_context->codec_tag = hints.codec_tag;
  m_context->coded_width = hints.width;
  m_context->coded_height = hints.height;
  m_context->time_base.num = 1;
  m_context->time_base.den = DVD_TIME_BASE;
  m_context->bits_per_coded_sample = hints.bitsperpixel;
  m_context->extra_hw_frames = 0;
  m_context->pkt_timebase.num = hints.fpsrate;
  m_context->pkt_timebase.den = hints.fpsscale;
  m_context->error_concealment = 0;
  m_context->err_recognition = AV_EF_IGNORE_ERR | AV_EF_EXPLODE;

  if (hints.extradata && hints.extrasize > 0)
  {
    m_context->extradata_size = hints.extrasize;
    m_context->extradata =
        static_cast<uint8_t*>(av_mallocz(hints.extrasize + AV_INPUT_BUFFER_PADDING_SIZE));
    memcpy(m_context->extradata, hints.extradata, hints.extrasize);
  }

  for (auto&& option : options.m_keys)
    av_opt_set(m_context, option.m_name.c_str(), option.m_value.c_str(), 0);

  m_portFormat->type = MMAL_ES_TYPE_VIDEO;
  m_portFormat->bitrate = 0;
  m_portFormat->flags = MMAL_ES_FORMAT_FLAG_FRAMED;
  m_portFormat->encoding = MMAL_ENCODING_UNKNOWN;
  m_portFormat->encoding_variant = MMAL_ENCODING_UNKNOWN;
  m_portFormat->es->video.par.num = 0;
  m_portFormat->es->video.par.den = 0;
  m_portFormat->es->video.frame_rate.num = hints.fpsrate;
  m_portFormat->es->video.frame_rate.den = hints.fpsscale;
  m_portFormat->es->video.width = VCOS_ALIGN_UP(m_context->coded_width, 32);
  m_portFormat->es->video.height = VCOS_ALIGN_UP(m_context->coded_height, 16);

  if ((uint32_t)hints.width < m_portFormat->es->video.width)
    m_portFormat->es->video.crop.width = hints.width;

  if ((uint32_t)hints.height < m_portFormat->es->video.height)
    m_portFormat->es->video.crop.height = hints.height;

  if (hints.forced_aspect)
  {
    m_portFormat->es->video.par.num = 1;
    m_portFormat->es->video.par.den = 1;
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
          m_portFormat->es->video.par.num = w;
          m_portFormat->es->video.par.den = h;
        }
      }
    }
  }

  m_hints = hints;
  m_state = MCS_OPENED;
  int status = avcodec_open2(m_context, m_codec, &codecOptions);
  if (status < 0)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecFFmpegMMAL::{} - failed to open codec: {} ({})",
              __FUNCTION__, GetStatusString(status), status);
    return false;
  }
  m_receive = true;
  return true;
}

const char* CDVDVideoCodecFFmpegMMAL::GetStatusString(int status)
{
  char msg[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(status, msg, AV_ERROR_MAX_STRING_SIZE);
  return msg;
}

bool CDVDVideoCodecFFmpegMMAL::AddData(const DemuxPacket& packet)
{
  MMALCodecState state = m_state;
  if (packet.pData == nullptr || packet.iSize <= 0)
  {
    m_state = MCS_CLOSING;
    return true;
  }
  else if (state == MCS_CLOSED || state == MCS_CLOSING)
  {
    return true;
  }
  else if (m_receive == false || state == MCS_FLUSHING || state == MCS_ERROR)
  {
    return false;
  }
  else
  {
    bool result = false;
    if (state == MCS_DECODING || state == MCS_FLUSHED || state == MCS_OPENED)
    {
      AVStatus status = AV_SUCCESS;
      int64_t ptsPacket = AV_NOPTS_VALUE;
      int64_t dtsPacket = AV_NOPTS_VALUE;

      if (packet.dts != DVD_NOPTS_VALUE)
        dtsPacket = static_cast<int64_t>(packet.dts / DVD_TIME_BASE * AV_TIME_BASE);

      if (!m_hints.ptsinvalid && packet.pts != DVD_NOPTS_VALUE)
        ptsPacket = static_cast<int64_t>(packet.pts / DVD_TIME_BASE * AV_TIME_BASE);

      AVPacket avpkt{};
      avpkt.pos = -1;
      avpkt.duration = static_cast<int64_t>(packet.duration / DVD_TIME_BASE * AV_TIME_BASE);
      avpkt.flags = 0;
      avpkt.stream_index = packet.iStreamId;
      avpkt.buf = NULL;
      avpkt.data = packet.pData;
      avpkt.size = packet.iSize;
      avpkt.dts = dtsPacket;
      avpkt.pts = ptsPacket;
      avpkt.side_data = static_cast<AVPacketSideData*>(packet.pSideData);
      avpkt.side_data_elems = packet.iSideDataElems;

      if ((m_codecControlFlags & DVD_CODEC_CTRL_DROP) != 0)
      {
        avpkt.flags |= AV_PKT_FLAG_DISCARD;
        m_droppedFrames++;
      }

      std::unique_lock<CCriticalSection> lock(m_bufferLock);
      status = static_cast<AVStatus>(avcodec_send_packet(m_context, &avpkt));
      lock.unlock();

      if (status == AV_SUCCESS)
      {
        result = true;
      }
      else if (status == AV_EAGAIN)
      {
        m_receive = false;
        result = false;
      }
      else if (status == AV_ECORRUPT)
      {
        result = true;
      }
      else if (status == AV_EOS)
      {
        m_state = MCS_CLOSING;
        m_receive = false;
        result = true;
      }
      else
      {
        CLog::Log(LOGERROR, "CDVDVideoCodecFFmpegMMAL::{} - failed to send buffer: {} ({})",
                  __FUNCTION__, GetStatusString(status), status);
      }
    }
    return result;
  }
  return false;
}

CDVDVideoCodec::VCReturn CDVDVideoCodecFFmpegMMAL::GetPicture(VideoPicture* pVideoPicture)
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
    CVideoBufferMMAL* buffer = nullptr;
    uint32_t available = m_bufferPool->Length(true);

    bool drain = state == MCS_CLOSING || state == MCS_FLUSHING || state == MCS_FLUSHED ||
                 (m_codecControlFlags & DVD_CODEC_CTRL_DRAIN) != 0;

    if (available > 0 && (drain || available >= GetAllowedReferences()) &&
        (buffer = dynamic_cast<CVideoBufferMMAL*>(m_bufferPool->Get(true))) != NULL)
    {
      bool drop = (m_codecControlFlags & DVD_CODEC_CTRL_DROP) != 0;

      pVideoPicture->Reset();
      pVideoPicture->SetParams(buffer->GetPicture());

      pVideoPicture->iFlags |= m_codecControlFlags;

      if (drop)
      {
        if (m_codec->id == AV_CODEC_ID_HEVC)
          buffer->ReleasePtr();
        pVideoPicture->iFlags |= DVP_FLAG_DROPPED;
      }

      pVideoPicture->videoBuffer = buffer;

      result = VC_PICTURE;
    }
    else if (state != MCS_CLOSING && m_receive)
    {
      if ((m_codecControlFlags & DVD_CODEC_CTRL_DRAIN) != 0)
        m_codecControlFlags &= ~DVD_CODEC_CTRL_DRAIN;

      result = VC_BUFFER;
    }
  }

  return result;
}

void CDVDVideoCodecFFmpegMMAL::Reset()
{
  if (m_state == MCS_DECODING)
  {
    Flush();
    m_receive = false;
    if ((m_codecControlFlags & DVD_CODEC_CTRL_DRAIN) != 0)
      m_codecControlFlags &= ~DVD_CODEC_CTRL_DRAIN;
  }
}

void CDVDVideoCodecFFmpegMMAL::Flush()
{
  AVPacket avpkt;
  av_init_packet(&avpkt);
  avpkt.data = nullptr;
  avpkt.size = 0;
  avpkt.dts = AV_NOPTS_VALUE;
  avpkt.pts = AV_NOPTS_VALUE;
  m_state = MCS_FLUSHING;
  std::unique_lock<CCriticalSection> lock(m_bufferLock);
  avcodec_send_packet(m_context, &avpkt);
}

void CDVDVideoCodecFFmpegMMAL::Process()
{
  MMALCodecState state = m_state;
  AVStatus status = AV_SUCCESS;
  uint32_t available = 0;
  AVFrame* frame = av_frame_alloc();
  CVideoBufferMMAL* buffer = nullptr;
  pthread_t tid = pthread_self();
  if (tid)
  {
    int policy;
    struct sched_param param;
    pthread_getschedparam(tid, &policy, &param);
    if (policy != SCHED_FIFO)
    {
      param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
      pthread_setschedparam(tid, SCHED_FIFO, &param);
    }
  }

  //CLog::Log(LOGDEBUG, "CDVDVideoCodecFFmpegMMAL::{} - decoder thread started", __FUNCTION__);
  while (!m_bStop && (state == MCS_DECODING || state == MCS_FLUSHING || state == MCS_CLOSING ||
                      state == MCS_FLUSHED))
  {
    available = m_bufferPool->Length();
    while (status == AV_SUCCESS && (available > 0 || state == MCS_FLUSHING))
    {
      std::unique_lock<CCriticalSection> lock(m_bufferLock);
      status = static_cast<AVStatus>(avcodec_receive_frame(m_context, frame));
      lock.unlock();
      if (status == AV_SUCCESS)
      {
        if (((frame->flags & AV_FRAME_FLAG_CORRUPT) == 0 &&
             (frame->flags & AV_FRAME_FLAG_DISCARD) == 0) &&
            (state == MCS_DECODING || state == MCS_FLUSHED))
        {
          if (m_bufferPool->Move(frame, m_codec->id, m_ptsCurrent == AV_NOPTS_VALUE,
                                 m_context->opaque))
          {
            if (state == MCS_FLUSHED)
              m_state = state = MCS_DECODING;
            m_ptsCurrent = frame->best_effort_timestamp;
            available--;
          }
          else
            m_droppedFrames++;
        }
        else
          m_droppedFrames++;

        av_frame_unref(frame);
      }
      else
      {
        if (status == AV_EOS && state == MCS_FLUSHING)
        {
          std::unique_lock<CCriticalSection> lock(m_bufferLock);
          avcodec_flush_buffers(m_context);
          lock.unlock();
          m_bufferPool->Flush();
          m_ptsCurrent = AV_NOPTS_VALUE;
          m_droppedFrames = -1;
          m_state = MCS_FLUSHED;
          m_receive = true;
        }
        break;
      }
    }
    if (status == AV_EAGAIN)
    {
      if (available > 0)
        m_receive = true;
      else
        m_receive = false;
    }
    else if (status == AV_EOS)
    {
      if ((state != MCS_FLUSHING && state != MCS_FLUSHED))
      {
        m_state = MCS_CLOSED;
        m_receive = false;
      }
      else if (available > 0)
        m_receive = true;
      else
        m_receive = false;
    }
    else
    {
      //error
    }
    status = AV_SUCCESS;
    if (!m_receive)
      KODI::TIME::Sleep(5ms);
    state = m_state;
  }
  av_frame_free(&frame);
  m_bufferPool->Flush();

  if (m_context)
  {
    avcodec_free_context(&m_context);
    m_context = nullptr;
  }
  //CLog::Log(LOGDEBUG, "CDVDVideoCodecFFmpegMMAL::{} - decoder thread stopped", __FUNCTION__);
}

void CDVDVideoCodecFFmpegMMAL::SetCodecControl(int flags)
{
  if (m_context)
  {
    if ((m_codecControlFlags & DVD_CODEC_CTRL_DROP_ANY) != (flags & DVD_CODEC_CTRL_DROP_ANY))
    {
      if ((flags & DVD_CODEC_CTRL_DROP_ANY) != 0)
      {
        m_context->skip_frame = AVDISCARD_NONREF;
        m_context->skip_idct = AVDISCARD_NONREF;
        m_context->skip_loop_filter = AVDISCARD_NONREF;
      }
      else
      {
        m_context->skip_frame = AVDISCARD_DEFAULT;
        m_context->skip_idct = AVDISCARD_DEFAULT;
        m_context->skip_loop_filter = AVDISCARD_DEFAULT;
      }
    }
  }

  m_codecControlFlags = flags;
}

bool CDVDVideoCodecFFmpegMMAL::GetCodecStats(double& pts, int& droppedFrames, int& skippedPics)
{
  if (!m_context)
    return false;

  if (m_ptsCurrent != AV_NOPTS_VALUE)
    pts = static_cast<double>(m_ptsCurrent) * DVD_TIME_BASE / AV_TIME_BASE;

  if (m_droppedFrames != -1)
    droppedFrames = m_droppedFrames + 1;
  else
    droppedFrames = -1;
  m_droppedFrames = -1;
  skippedPics = -1;
  return true;
}
