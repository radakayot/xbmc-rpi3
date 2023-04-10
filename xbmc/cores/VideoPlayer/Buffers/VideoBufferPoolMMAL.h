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
  CVideoBuffer* Get(bool rendered);
  void Return(int id) override;

  void Put(CVideoBufferMMAL* buffer);
  bool Move(AVFrame* frame, AVCodecID codecId, bool flushed, void* envPtr = nullptr);
  void Flush();
  uint32_t Length(bool rendered = false);

  void Configure(AVPixelFormat format, int size) override;
  void Configure(MMALFormat portFormat, VideoPicture* pBasePicture, uint32_t count, int32_t size);

  bool IsConfigured() override;
  bool IsCompatible(AVPixelFormat format, int size) override;

  void Released(CVideoBufferManager& videoBufferManager) override;
  void SetReleaseCallback(IVideoBufferPoolMMALCallback callback = nullptr,
                          void* userdata = nullptr);
  void Discard(CVideoBufferManager* bm, ReadyToDispose cb) override;
  void Dispose();

protected:
  void Initialize();
  void InitializeBuffers(VideoPicture* pBasePicture);

  std::vector<CVideoBufferMMAL*> m_all;
  std::deque<int> m_used;
  std::deque<int> m_free;
  std::deque<int> m_ready;

  CVideoBufferManager* m_bufferManager = nullptr;
  ReadyToDispose m_disposeCallback{NULL};

  CCriticalSection m_poolLock;

private:
  static int32_t ProcessBufferCallback(MMALPool pool, MMALBufferHeader header, void* userdata);

  MMALComponent m_component{nullptr};
  MMALFormat m_portFormat{nullptr};
  MMALPort m_port{nullptr};
  MMALPool m_pool{nullptr};

  int m_size{-1};

  IVideoBufferPoolMMALCallback m_callback{NULL};
  void* m_userdata{nullptr};
};

} // namespace MMAL