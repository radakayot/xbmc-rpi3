/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/VideoPlayer/Buffers/VideoBuffer.h"
#include "cores/VideoPlayer/Buffers/VideoBufferMMAL.h"
#include "cores/VideoPlayer/DVDStreamInfo.h"
#include "threads/Condition.h"

#include <memory>

#include <interface/mmal/mmal.h>
#include <interface/mmal/mmal_events.h>
#include <interface/mmal/util/mmal_default_components.h>
#include <interface/mmal/util/mmal_util.h>
#include <interface/mmal/util/mmal_util_params.h>
#include <interface/mmal/vc/mmal_vc_msgs.h>

extern "C"
{
#include <libavcodec/codec_id.h>
#include <libavutil/pixfmt.h>
}
namespace MMAL
{
class CVideoBufferMMAL;
class CVideoBufferPoolMMAL;

typedef void (*IVideoBufferPoolMMALCallback)(CVideoBufferPoolMMAL* pool,
                                             CVideoBufferMMAL* buffer,
                                             void* userdata);

class CVideoBufferPoolMMAL : public IVideoBufferPool
{
public:
  static uint32_t TranslateCodec(AVCodecID codec);
  static uint32_t TranslateFormat(AVPixelFormat format);
  static uint32_t TranslateColorSpace(AVColorSpace space);
  static AVPixelFormat TranslatePortFormat(uint32_t format);

  CVideoBufferPoolMMAL() = default;
  ~CVideoBufferPoolMMAL() override;

  CVideoBuffer* Get() override;
  void Return(int id) override;

  void Configure(AVPixelFormat format, int size) override;
  bool IsConfigured() override;
  bool IsCompatible(AVPixelFormat format, int size) override;
  void Release();

  void Released(CVideoBufferManager& videoBufferManager) override;

protected:
  void Initialize();
  void InitializeBuffers(VideoPicture* pBasePicture);

  std::vector<CVideoBufferMMAL*> m_all;
  std::deque<int> m_used;
  std::deque<int> m_free;

  CCriticalSection m_poolLock;

private:
  MMALComponent m_component{nullptr};
  MMALPort m_port{nullptr};
  MMALFormat m_portFormat{nullptr};
  AVPixelFormat m_format{AV_PIX_FMT_NONE};
};

} // namespace MMAL