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

void CVideoBufferMMAL::ProcessReleaseCallback(MMALBufferHeader header)
{
  if (header && header->priv && header->user_data)
  {
    CVideoBufferMMAL* buffer = static_cast<CVideoBufferMMAL*>(header->user_data);
    if (header->priv->owner && buffer->m_pool)
    {
      std::shared_ptr<IVideoBufferPool> pool = buffer->m_pool->GetPtr();
      buffer->m_pool = nullptr;
      header->priv->owner = nullptr;
      pool->Return(buffer->m_id);
    }
    else
    {
      delete buffer;
    }
  }
}

CVideoBufferMMAL::CVideoBufferMMAL(MMALPort port, int id, AVPixelFormat format) : CVideoBuffer(id)
{
  m_pixFormat = format;
  m_pool = nullptr;
  m_name = m_name + std::to_string(id);
  uint32_t length = VCOS_ALIGN_UP(sizeof(*m_header), 8);
  length += VCOS_ALIGN_UP(sizeof(*m_header->type), 8);
  length += 256;
  length += VCOS_ALIGN_UP(sizeof(*m_header->priv), 8);

  void* header = vcos_calloc(1, length, m_name.c_str());
  if (header)
  {
    memset(header, 0, length);
    m_header = (MMAL_BUFFER_HEADER_T*)header;
    m_header->type = (MMAL_BUFFER_HEADER_TYPE_SPECIFIC_T*)&m_header[1];
    m_header->priv = (MMAL_BUFFER_HEADER_PRIVATE_T*)&m_header->type[1];
    m_header->user_data = this;
    m_header->priv->owner = nullptr;
    m_header->priv->refcount = 0;
    m_header->priv->payload_context = port;
    m_header->priv->pf_release = CVideoBufferMMAL::ProcessReleaseCallback;
  }
  else
    CLog::Log(LOGERROR, "CVideoBufferMMAL::{} - failed to allocate buffer", __FUNCTION__);
}

CVideoBufferMMAL::~CVideoBufferMMAL()
{
  Free();
  if (m_header)
  {
    if (m_header->priv->refcount == 0)
    {
      vcos_free(m_header);
      m_header = nullptr;
    }
    else
    {
      m_header->priv->owner = nullptr;
      m_pool = nullptr;
      Release();
    }
  }
}

bool CVideoBufferMMAL::Alloc(uint32_t size)
{
  if (m_header)
  {
    MMALPort port = (MMALPort)m_header->priv->payload_context;
    MMALPortPrivate priv = (MMALPortPrivate)port->priv;
    uint8_t* payload = priv->pf_payload_alloc(port, size);
    if (payload)
    {
      m_header->data = payload;
      m_header->alloc_size = size;
      m_header->priv->payload = payload;
      m_header->priv->pf_payload_free = priv->pf_payload_free;
      m_header->priv->payload_size = size;
      return true;
    }
  }
  return false;
}

void CVideoBufferMMAL::Free()
{
  if (m_header && m_header->priv)
  {
    if (m_header->priv->pf_payload_free && m_header->priv->payload_size > 0)
      m_header->priv->pf_payload_free(m_header->priv->payload_context, m_header->priv->payload);
    m_header->data = nullptr;
    m_header->alloc_size = 0;
    m_header->priv->payload = nullptr;
    m_header->priv->payload_context = nullptr;
    m_header->priv->pf_payload_free = NULL;
    m_header->priv->payload_size = 0;
    m_header->priv->owner = nullptr;
  }
}

void CVideoBufferMMAL::Acquire()
{
  Acquire(true);
}

void CVideoBufferMMAL::Acquire(bool withLock)
{
  std::unique_lock<CCriticalSection> lock(m_bufferLock);
  mmal_buffer_header_acquire(m_header);
  m_refCount = m_header->priv->refcount;
  if (withLock)
    Lock();
}

void CVideoBufferMMAL::Acquire(std::shared_ptr<IVideoBufferPool> pool)
{
  std::unique_lock<CCriticalSection> lock(m_bufferLock);
  mmal_buffer_header_acquire(m_header);
  m_refCount = m_header->priv->refcount;
  m_pool = std::move(pool);
  m_header->priv->owner = m_pool.get();
}

void CVideoBufferMMAL::Release()
{
  std::unique_lock<CCriticalSection> lock(m_bufferLock);
  if (m_header->priv->refcount > 0)
  {
    mmal_buffer_header_release(m_header);
    m_refCount = m_header->priv->refcount;
  }
}

bool CVideoBufferMMAL::Lock()
{
  if (!m_locked)
    m_locked = mmal_buffer_header_mem_lock(m_header) == MMAL_SUCCESS;
  return m_locked;
}

void CVideoBufferMMAL::Unlock()
{
  if (m_locked)
  {
    mmal_buffer_header_mem_unlock(m_header);
    m_locked = false;
  }
}

bool CVideoBufferMMAL::IsRendering()
{
  return m_rendering;
}

void CVideoBufferMMAL::SetRendering(bool rendering)
{
  m_rendering = rendering;
}

uint8_t* CVideoBufferMMAL::GetMemPtr()
{
  return m_header->data;
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
  //m_width = width;
  //m_height = height;
  for (uint32_t i = 0; i < m_header->type->video.planes; i++)
  {
    m_header->type->video.offset[i] = planeOffsets[i];
    m_header->type->video.pitch[i] = strides[i];
  }
}

void CVideoBufferMMAL::ReadPicture(const VideoPicture& videoPicture)
{
  if (m_header)
  {
    if (videoPicture.pts == DVD_NOPTS_VALUE)
      m_header->pts = MMAL_TIME_UNKNOWN;
    else
      m_header->pts = static_cast<int64_t>(videoPicture.pts / DVD_TIME_BASE * AV_TIME_BASE);

    if (videoPicture.dts == DVD_NOPTS_VALUE)
      m_header->dts = MMAL_TIME_UNKNOWN;
    else
      m_header->dts = static_cast<int64_t>(videoPicture.dts / DVD_TIME_BASE * AV_TIME_BASE);

    if ((m_header->flags & MMAL_BUFFER_HEADER_FLAG_DROPPED) == 0 &&
        (videoPicture.iFlags & DVP_FLAG_DROPPED) != 0)
      m_header->flags |= MMAL_BUFFER_HEADER_FLAG_DROPPED;
  }
}

void CVideoBufferMMAL::WritePicture(VideoPicture* pVideoPicture)
{
  if (m_header)
  {
    if (m_header->pts == MMAL_TIME_UNKNOWN)
      pVideoPicture->pts = DVD_NOPTS_VALUE;
    else
      pVideoPicture->pts = static_cast<double>(m_header->pts) * DVD_TIME_BASE / AV_TIME_BASE;

    if (m_header->dts == MMAL_TIME_UNKNOWN)
      pVideoPicture->dts = DVD_NOPTS_VALUE;
    else
      pVideoPicture->dts = static_cast<double>(m_header->dts) * DVD_TIME_BASE / AV_TIME_BASE;

    if ((m_header->flags & MMAL_BUFFER_HEADER_FLAG_DROPPED) != 0 &&
        (pVideoPicture->iFlags & DVP_FLAG_DROPPED) == 0)
      pVideoPicture->iFlags |= DVP_FLAG_DROPPED;

    if ((m_header->flags & MMAL_BUFFER_HEADER_FLAG_SEEK) != 0)
    {
      m_header->flags &= ~MMAL_BUFFER_HEADER_FLAG_SEEK;
      m_header->flags |= MMAL_BUFFER_HEADER_FLAG_DISCONTINUITY;
    }
  }
}