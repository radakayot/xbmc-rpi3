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

class CVideoBufferMMAL : public CVideoBuffer
{
public:
  CVideoBufferMMAL() = delete;
  CVideoBufferMMAL(int id, MMALBufferHeader header);
  ~CVideoBufferMMAL() override;

  void Acquire() override;
  void Acquire(bool lockMemory);
  void Acquire(std::shared_ptr<IVideoBufferPool> pool) override;
  void Release() override;

  void SetDimensions(int width, int height);
  void SetDimensions(int width, int height, const int (&strides)[YuvImage::MAX_PLANES]) override;
  void SetDimensions(int width,
                     int height,
                     const int (&strides)[YuvImage::MAX_PLANES],
                     const int (&planeOffsets)[YuvImage::MAX_PLANES]) override;

  void GetPlanes(uint8_t* (&planes)[YuvImage::MAX_PLANES]) override;
  void GetStrides(int (&strides)[YuvImage::MAX_PLANES]) override;
  uint8_t* GetMemPtr() override;

  const VideoPicture& GetPicture() const { return m_picture; }
  void SetBasePicture(VideoPicture* pBasePicture);

  int GetRenderIndex();
  void SetRenderIndex(int renderIndex);

  MMALBufferHeader GetHeader() { return m_header; };
  int GetSize() { return m_header->alloc_size; };

  MMALFormat GetPortFormat();
  void SetPortFormat(MMALFormat portFormat);

  bool UpdateBufferFromFrame(AVFrame* frame, AVCodecID codecId, bool flushed, AVZcEnvPtr envPtr = nullptr);

  void ReleasePtr();
  void Dispose();

protected:
  VideoPicture m_picture;

private:
  void UpdatePictureParams();

  MMALBufferHeader m_header{nullptr};
  MMALFormat m_portFormat{nullptr};
  bool m_locked{false};
  bool m_disposing{false};

  int m_renderIndex{-1};
  AVMMALZcRefPtr m_refPtr{nullptr};

  CCriticalSection m_critSection;
};

} // namespace MMAL