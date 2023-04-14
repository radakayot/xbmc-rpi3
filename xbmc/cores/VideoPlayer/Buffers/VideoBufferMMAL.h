/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/VideoPlayer/Buffers/VideoBuffer.h"
#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodec.h"
#include "threads/CriticalSection.h"

#include <interface/mmal/mmal.h>

extern "C"
{
#include <libavcodec/mmal_zc.h>
}

#define MMAL_BUFFER_HEADER_FLAG_ZEROCOPY (MMAL_BUFFER_HEADER_FLAG_USER0)
#define MMAL_BUFFER_HEADER_FLAG_DROPPED (MMAL_BUFFER_HEADER_FLAG_USER1)
#define MMAL_BUFFER_HEADER_FLAG_SEEK (MMAL_BUFFER_HEADER_FLAG_USER2)

namespace MMAL
{
typedef MMAL_STATUS_T MMALStatus;
typedef MMAL_COMPONENT_T* MMALComponent;
typedef MMAL_PORT_T* MMALPort;
typedef MMAL_POOL_T* MMALPool;
typedef MMAL_QUEUE_T* MMALQueue;
typedef MMAL_ES_FORMAT_T* MMALFormat;
typedef MMAL_BUFFER_HEADER_T* MMALBufferHeader;
typedef MMAL_PARAMETER_HEADER_T* MMALParameterHeader;
typedef MMAL_PORT_BH_CB_T MMALPortBufferHeaderCallback;

// Internal struct to test flush and disable without lock
typedef struct MMAL_PORT_PRIVATE_T
{
  uint32_t* core;
  uint32_t* module;
  uint32_t* clock;

  MMALStatus (*pf_set_format)(MMALPort port);
  MMALStatus (*pf_enable)(MMALPort port, MMALPortBufferHeaderCallback);
  MMALStatus (*pf_disable)(MMALPort port);
  MMALStatus (*pf_send)(MMALPort port, MMALBufferHeader);
  MMALStatus (*pf_flush)(MMALPort port);
  MMALStatus (*pf_parameter_set)(MMALPort port, const MMALParameterHeader param);
  MMALStatus (*pf_parameter_get)(MMALPort port, MMALParameterHeader param);
  MMALStatus (*pf_connect)(MMALPort port, MMALPort other_port);

  uint8_t* (*pf_payload_alloc)(MMALPort port, uint32_t payload_size);
  //void (*pf_payload_free)(MMALPort port, uint8_t* payload);
  void (*pf_payload_free)(void* port, void* payload);

} MMAL_PORT_PRIVATE_T, *MMALPortPrivate;

typedef MMAL_PORT_USERDATA_T* MMALPortUserData;
typedef MMAL_EVENT_FORMAT_CHANGED_T* MMALFormatChangedEventArgs;
typedef MMAL_EVENT_END_OF_STREAM_T* MMALEndOfStreamEventArgs;
typedef MMAL_EVENT_PARAMETER_CHANGED_T* MMALParameterChangedEventArgs;

class CVideoBufferMMAL : public CVideoBuffer
{
public:
  CVideoBufferMMAL() = delete;
  CVideoBufferMMAL(MMALPort port, int id, AVPixelFormat format);
  ~CVideoBufferMMAL() override;

  bool Alloc(uint32_t size);
  void Free();

  void Acquire() override;
  void Acquire(bool withLock);
  void Acquire(std::shared_ptr<IVideoBufferPool> pool) override;
  void Release() override;

  bool Lock();
  void Unlock();

  uint8_t* GetMemPtr() override;

  bool IsRendering();
  void SetRendering(bool rendering);

  MMALBufferHeader GetHeader() { return m_header; };
  int GetSize() { return m_header->alloc_size; };
  MMALFormat GetPortFormat() { return m_portFormat; };
  void SetPortFormat(MMALFormat format) { m_portFormat = format; };

  void SetDimensions(int width, int height);
  void SetDimensions(int width, int height, const int (&strides)[YuvImage::MAX_PLANES]) override;
  void SetDimensions(int width,
                     int height,
                     const int (&strides)[YuvImage::MAX_PLANES],
                     const int (&planeOffsets)[YuvImage::MAX_PLANES]) override;

  void GetPlanes(uint8_t* (&planes)[YuvImage::MAX_PLANES]) override;
  void GetStrides(int (&strides)[YuvImage::MAX_PLANES]) override;

  void SetPictureParams(VideoPicture* pVideoPicture);

private:
  static void ProcessReleaseCallback(MMALBufferHeader header);
  std::string m_name{"MMALBufferHeader "};
  MMALBufferHeader m_header{nullptr};
  MMALFormat m_portFormat{nullptr};

  bool m_locked{false};
  bool m_rendering{false};

  CCriticalSection m_bufferLock;
};

} // namespace MMAL