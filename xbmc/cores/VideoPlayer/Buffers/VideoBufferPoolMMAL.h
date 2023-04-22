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

#define MMAL_THREAD_PRI_MIN (sched_get_priority_min(SCHED_RR))
#define MMAL_THREAD_PRI_MAX (sched_get_priority_max(SCHED_RR))
#define MMAL_THREAD_PRI_NORMAL ((MMAL_THREAD_PRI_MAX + MMAL_THREAD_PRI_MIN) / 2)
#define MMAL_THREAD_PRI_HIGH \
  (MMAL_THREAD_PRI_NORMAL + ((MMAL_THREAD_PRI_MAX - MMAL_THREAD_PRI_NORMAL) / 2))
#define MMAL_THREAD_PRI_LOW \
  (MMAL_THREAD_PRI_NORMAL - ((MMAL_THREAD_PRI_NORMAL - MMAL_THREAD_PRI_MIN) / 2))

#define mmal_component_set_priority(component, policy, priority) \
  { \
    struct sched_param sp \
    { \
      .sched_priority = priority \
    }; \
    *(int*)((uint8_t*)component->priv + 28) = priority; \
    pthread_setschedparam(((VCOS_THREAD_T*)((uint8_t*)component->priv + 36))->thread, policy, \
                          &sp); \
  }

#define thread_set_priority(policy, priority) \
  { \
    struct sched_param sp \
    { \
      .sched_priority = priority \
    }; \
    pthread_setschedparam(pthread_self(), policy, &sp); \
  }

namespace MMAL
{
class CVideoBufferPoolMMAL : public IVideoBufferPool
{
public:
  static uint32_t TranslateCodec(AVCodecID codec);
  static uint32_t TranslateFormat(AVPixelFormat format);
  static uint32_t TranslateColorSpace(AVColorSpace space);
  static AVPixelFormat TranslatePortFormat(uint32_t format);

  CVideoBufferPoolMMAL();
  ~CVideoBufferPoolMMAL() override;

  CVideoBuffer* Get() override;
  CVideoBuffer* Get(int size);
  void Return(int id) override;

  void Configure(AVPixelFormat format, int size) override;
  bool IsConfigured() override;
  bool IsCompatible(AVPixelFormat format, int size) override;

  void Release();

protected:
  std::vector<CVideoBufferMMAL*> m_all;
  std::deque<int> m_used;
  std::deque<int> m_free;

private:
  static MMALComponent m_component;
  MMALPort m_port{nullptr};
  MMALFormat m_portFormat{nullptr};
  AVPixelFormat m_format{AV_PIX_FMT_NONE};
  int m_size{0};
  CCriticalSection m_poolLock;
};

} // namespace MMAL