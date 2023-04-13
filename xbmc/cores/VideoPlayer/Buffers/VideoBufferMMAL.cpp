/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "VideoBufferMMAL.h"

#include "ServiceBroker.h"
#include "VideoBufferPoolMMAL.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "windowing/dmx/WinSystemDmx.h"

#include <interface/mmal/core/mmal_buffer_private.h>
#include <interface/vcsm/user-vcsm.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

using namespace MMAL;

CVideoBufferMMAL::CVideoBufferMMAL(int id, MMALBufferHeader header) : CVideoBuffer(id)
{
  m_pixFormat = AV_PIX_FMT_NONE;
  m_pool = nullptr;
  m_header = header;
  m_header->user_data = this;
  m_refCount = 0;
  m_refPtr = nullptr;
}

CVideoBufferMMAL::~CVideoBufferMMAL()
{
  m_refCount = 0;
  Dispose();
}

void CVideoBufferMMAL::Dispose()
{
  std::unique_lock<CCriticalSection> lock(m_critSection);
  if (m_header)
  {
    if (m_refCount <= 0)
    {
      if (m_picture.videoBuffer)
        m_picture.videoBuffer = nullptr;

      if (m_locked)
        mmal_buffer_header_mem_unlock(m_header);

      if (m_refPtr)
      {
        mmal_buffer_header_reset(m_header);
        m_header->cmd = 0;
        m_header->flags = 0;
        m_header->data = (uint8_t*)-1;
        m_header->offset = 0;
        m_header->length = 0;
        m_header->alloc_size = 0;
        m_header->dts = MMAL_TIME_UNKNOWN;
        m_header->pts = MMAL_TIME_UNKNOWN;

        av_mmal_zc_unref(m_refPtr);
        m_refPtr = nullptr;
      }
      else
      {
        if (m_header->priv->payload_context && m_header->priv->payload)
        {
          MMALPort port = (MMALPort)m_header->priv->payload_context;
          ((MMALPortPrivate)port->priv)->pf_payload_free(port, (uint8_t*)m_header->priv->payload);
        }
      }
      m_header->user_data = nullptr;
      m_header->priv->refcount = 0;
      m_header->priv->reference = nullptr;

      vcos_free(m_header);
      m_portFormat = nullptr;
      m_header = nullptr;
      m_pool = nullptr;
    }
  }
  m_disposing = true;
}

void CVideoBufferMMAL::Acquire()
{
  Acquire(true);
}

void CVideoBufferMMAL::Acquire(bool lockMemory)
{
  std::unique_lock<CCriticalSection> lock(m_critSection);
  mmal_buffer_header_acquire(m_header);
  m_refCount = m_header->priv->refcount;
  if (lockMemory && !m_locked)
    m_locked = mmal_buffer_header_mem_lock(m_header) == MMAL_SUCCESS;

  int64_t dtsPicture = MMAL_TIME_UNKNOWN;
  int64_t ptsPicture = MMAL_TIME_UNKNOWN;

  if (m_picture.dts != DVD_NOPTS_VALUE)
    dtsPicture = static_cast<int64_t>(m_picture.dts / DVD_TIME_BASE * AV_TIME_BASE);
  if (m_picture.pts != DVD_NOPTS_VALUE)
    ptsPicture = static_cast<int64_t>(m_picture.pts / DVD_TIME_BASE * AV_TIME_BASE);

  if (dtsPicture != MMAL_TIME_UNKNOWN)
    m_header->dts = dtsPicture;
  if (ptsPicture != MMAL_TIME_UNKNOWN)
    m_header->pts = ptsPicture;

  if ((m_picture.iFlags & DVP_FLAG_DROPPED) != 0 &&
      (m_header->flags & MMAL_BUFFER_HEADER_FLAG_DROPPED) == 0)
    m_header->flags |= MMAL_BUFFER_HEADER_FLAG_DROPPED;
}

void CVideoBufferMMAL::Acquire(std::shared_ptr<IVideoBufferPool> pool)
{
  std::unique_lock<CCriticalSection> lock(m_critSection);
  if (m_pool)
    m_pool = nullptr;

  m_refCount = m_header->priv->refcount;
  m_pool = std::move(pool);

  if (m_header->pts == MMAL_TIME_UNKNOWN)
    m_picture.pts = DVD_NOPTS_VALUE;
  else
    m_picture.pts = static_cast<double>(m_header->pts) * DVD_TIME_BASE / AV_TIME_BASE;

  if (m_header->dts == MMAL_TIME_UNKNOWN)
    m_picture.dts = DVD_NOPTS_VALUE;
  else
    m_picture.dts = static_cast<double>(m_header->dts) * DVD_TIME_BASE / AV_TIME_BASE;

  m_picture.iFlags = 0;
  if ((m_header->flags & MMAL_BUFFER_HEADER_FLAG_DROPPED) != 0)
    m_picture.iFlags |= DVP_FLAG_DROPPED;

  if ((m_header->flags & MMAL_BUFFER_HEADER_FLAG_SEEK) != 0)
  {
    m_picture.iFlags &= ~MMAL_BUFFER_HEADER_FLAG_SEEK;
    m_picture.iFlags |= MMAL_BUFFER_HEADER_FLAG_DISCONTINUITY;
  }
}

void CVideoBufferMMAL::Release()
{
  std::unique_lock<CCriticalSection> lock(m_critSection);
  m_refCount = m_header->priv->refcount;
  if (m_refCount == 1 && m_pool)
  {
    if (m_picture.videoBuffer)
      m_picture.videoBuffer = nullptr;

    if (m_refPtr)
    {
      av_mmal_zc_unref(m_refPtr);
      m_refPtr = nullptr;
    }

    if (m_refCount != 0)
      m_refCount = 0;

    if (m_locked)
    {
      mmal_buffer_header_mem_unlock(m_header);
      m_locked = false;
    }
    mmal_buffer_header_release(m_header);

    if (m_pool)
      m_pool = nullptr;
  }
  else if (m_refCount > 1)
  {
    mmal_buffer_header_release(m_header);
  }
}

void CVideoBufferMMAL::SetBasePicture(VideoPicture* pBasePicture)
{
  memcpy(&m_picture, pBasePicture, sizeof(VideoPicture));
}

void CVideoBufferMMAL::GetPlanes(uint8_t* (&planes)[YuvImage::MAX_PLANES])
{
  for (uint32_t i = 0; i < YuvImage::MAX_PLANES; i++)
    planes[i] = m_header->data + m_header->type->video.offset[i];
}

void CVideoBufferMMAL::GetStrides(int (&strides)[YuvImage::MAX_PLANES])
{
  for (uint32_t i = 0; i < YuvImage::MAX_PLANES; i++)
    strides[i] = m_header->type->video.pitch[i];
}

void CVideoBufferMMAL::SetDimensions(int width, int height)
{
  int strides[YuvImage::MAX_PLANES];
  for (uint32_t i = 0; i < m_header->type->video.planes; i++)
  {
    strides[i] = m_header->type->video.pitch[i];
  }
  SetDimensions(width, height, strides);
}

void CVideoBufferMMAL::SetDimensions(int width,
                                     int height,
                                     const int (&strides)[YuvImage::MAX_PLANES])
{
  int planeOffsets[YuvImage::MAX_PLANES];
  for (uint32_t i = 0; i < m_header->type->video.planes; i++)
  {
    planeOffsets[i] = m_header->type->video.offset[i];
  }
  SetDimensions(width, height, strides, planeOffsets);
}

void CVideoBufferMMAL::SetDimensions(int width,
                                     int height,
                                     const int (&strides)[YuvImage::MAX_PLANES],
                                     const int (&planeOffsets)[YuvImage::MAX_PLANES])
{
  m_picture.iWidth = width;
  m_picture.iHeight = height;

  for (uint32_t i = 0; i < m_header->type->video.planes; i++)
  {
    m_header->type->video.offset[i] = planeOffsets[i];
    m_header->type->video.pitch[i] = strides[i];
  }
}

uint8_t* CVideoBufferMMAL::GetMemPtr()
{
  return m_header->data;
}

int CVideoBufferMMAL::GetRenderIndex()
{
  return m_renderIndex;
}

void CVideoBufferMMAL::SetRenderIndex(int renderIndex)
{
  m_renderIndex = renderIndex;
}

MMALFormat CVideoBufferMMAL::GetPortFormat()
{
  return m_portFormat;
}

void CVideoBufferMMAL::SetPortFormat(MMALFormat portFormat)
{
  m_portFormat = portFormat;
  m_pixFormat = CVideoBufferPoolMMAL::TranslatePortFormat(portFormat->encoding);
}

void CVideoBufferMMAL::ReleasePtr()
{
  if (m_refPtr)
  {
    av_mmal_zc_unref(m_refPtr);
    m_refPtr = nullptr;
  }
}

bool CVideoBufferMMAL::UpdateBufferFromFrame(AVFrame* frame,
                                             AVCodecID codecId,
                                             bool flushed,
                                             AVZcEnvPtr envPtr)
{
  if (codecId == AV_CODEC_ID_HEVC)
  {
    if (m_refPtr)
    {
      av_mmal_zc_unref(m_refPtr);
      m_refPtr = nullptr;
    }

    if (frame == nullptr || frame->buf[0] == nullptr || av_mmal_zc_vc_handle(frame->buf[0]) == -1)
    {
      mmal_buffer_header_reset(m_header);
      m_header->cmd = 0;
      m_header->flags = MMAL_BUFFER_HEADER_FLAG_FRAME_END | MMAL_BUFFER_HEADER_FLAG_DROPPED;
      m_header->data = (uint8_t*)-1;
      m_header->offset = 0;
      m_header->length = 0;
      m_header->alloc_size = 0;
      m_header->dts = MMAL_TIME_UNKNOWN;
      m_header->pts = MMAL_TIME_UNKNOWN;
    }
    else
    {
      m_refPtr = av_mmal_zc_ref(envPtr, frame, (AVPixelFormat)frame->format, 1);
      mmal_buffer_header_reset(m_header);
      m_header->cmd = 0;
      m_header->flags = MMAL_BUFFER_HEADER_FLAG_FRAME_END;
      m_header->data = (uint8_t*)av_mmal_zc_vc_handle(m_refPtr);
      m_header->offset = av_mmal_zc_offset(m_refPtr);
      m_header->length = av_mmal_zc_length(m_refPtr);
      m_header->alloc_size = av_mmal_zc_numbytes(m_refPtr);
      m_header->dts = MMAL_TIME_UNKNOWN;
      m_header->pts = MMAL_TIME_UNKNOWN;

      if (frame->pts != AV_NOPTS_VALUE)
        m_header->pts = frame->pts;

      if (frame->pkt_dts != AV_NOPTS_VALUE)
        m_header->dts = frame->pkt_dts;

      if (flushed)
        m_header->flags |= MMAL_BUFFER_HEADER_FLAG_DISCONTINUITY;
    }
    return true;
  }
  else
  {
    if (frame == nullptr)
    {
      mmal_buffer_header_reset(m_header);
      m_header->cmd = 0;
      m_header->flags = MMAL_BUFFER_HEADER_FLAG_FRAME_END | MMAL_BUFFER_HEADER_FLAG_DROPPED;
      m_header->length = 0;
      m_header->dts = MMAL_TIME_UNKNOWN;
      m_header->pts = MMAL_TIME_UNKNOWN;
    }
    else
    {
      mmal_buffer_header_reset(m_header);
      m_header->cmd = 0;
      m_header->flags = MMAL_BUFFER_HEADER_FLAG_FRAME_END;
      if (!m_locked)
        m_locked = mmal_buffer_header_mem_lock(m_header) == MMAL_SUCCESS;
      if (m_locked)
      {
        m_header->length = av_image_copy_to_buffer(
            m_header->data, m_header->alloc_size, frame->data, frame->linesize,
            (AVPixelFormat)frame->format, VCOS_ALIGN_UP(frame->width, 32),
            VCOS_ALIGN_UP(frame->height, 16), 1);
        mmal_buffer_header_mem_unlock(m_header);
        m_locked = false;
        m_header->type->video.planes = YuvImage::MAX_PLANES;

        for (uint32_t i = 0; i < m_header->type->video.planes; i++)
        {
          if (i == 0)
            m_header->type->video.offset[i] = 0;
          else
            m_header->type->video.offset[i] =
                m_header->type->video.offset[i - 1] + m_header->type->video.pitch[i - 1];

          m_header->type->video.pitch[i] = frame->linesize[i];
        }
      }

      m_header->dts = MMAL_TIME_UNKNOWN;
      m_header->pts = MMAL_TIME_UNKNOWN;

      if (frame->pts != AV_NOPTS_VALUE)
        m_header->pts = frame->pts;

      if (frame->pkt_dts != AV_NOPTS_VALUE)
        m_header->dts = frame->pkt_dts;

      if (flushed)
        m_header->flags |= MMAL_BUFFER_HEADER_FLAG_DISCONTINUITY;
    }
    return true;
  }
  return false;
}