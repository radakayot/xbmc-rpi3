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

int32_t CVideoBufferPoolMMAL::ProcessBufferCallback(MMALPool pool,
                                                    MMALBufferHeader header,
                                                    void* userdata)
{
  CVideoBufferPoolMMAL* bufferPool = static_cast<CVideoBufferPoolMMAL*>(userdata);
  if (bufferPool && header->user_data)
  {
    CVideoBufferMMAL* buffer = static_cast<CVideoBufferMMAL*>(header->user_data);
    if (buffer)
    {
      bufferPool->Return(buffer->GetId());
      if (bufferPool->m_callback)
        bufferPool->m_callback(bufferPool, buffer, bufferPool->m_userdata);
      return MMAL_FALSE;
    }
  }
  return MMAL_TRUE;
}
/*
CVideoBufferPoolMMAL::CVideoBufferPoolMMAL()
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
}
*/
CVideoBufferPoolMMAL::~CVideoBufferPoolMMAL()
{

  //CLog::Log(LOGDEBUG, "CVideoBufferPoolMMAL::{} - destroying", __FUNCTION__);
  Dispose();
  if (m_portFormat)
  {
    mmal_format_free(m_portFormat);
    m_portFormat = nullptr;
  }

  if (m_component)
  {
    //for (auto buf : m_all)
    //  delete buf;
    //m_all.clear();

    if (mmal_component_release(m_component) != MMAL_SUCCESS)
      CLog::Log(LOGERROR, "CVideoBufferPoolMMAL::{} - failed to release component", __FUNCTION__);
    m_component = nullptr;
  }

  //CLog::Log(LOGDEBUG, "CVideoBufferPoolMMAL::{} - destroyed", __FUNCTION__);
}

void CVideoBufferPoolMMAL::Initialize()
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
      m_port->userdata = (struct MMAL_PORT_USERDATA_T*)this;
      m_port->buffer_num = 0;
      m_port->buffer_size = 0;
      m_port->format->encoding = MMAL_ENCODING_UNKNOWN;
      m_port->format->encoding_variant = MMAL_ENCODING_UNKNOWN;
      m_portFormat = mmal_format_alloc();
      m_portFormat->type = MMAL_ES_TYPE_VIDEO;
      mmal_port_parameter_set_uint32(m_port, MMAL_PARAMETER_EXTRA_BUFFERS, 0);
      mmal_port_parameter_set_boolean(m_port, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
      if (mmal_port_format_commit(m_port) == MMAL_SUCCESS)
      {
        mmal_format_full_copy(m_portFormat, m_port->format);
        m_portFormat->encoding = MMAL_ENCODING_UNKNOWN;
        m_portFormat->encoding_variant = MMAL_ENCODING_UNKNOWN;
        m_portFormat->extradata = nullptr;
        m_portFormat->extradata_size = 0;
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

void CVideoBufferPoolMMAL::Dispose()
{
  if (m_port)
  {
    if (m_pool)
    {
      std::unique_lock<CCriticalSection> lock(m_poolLock);
      mmal_pool_callback_set(m_pool, NULL, nullptr);

      for (auto buf : m_all)
        buf->Dispose();

      if (m_pool->header)
      {
        vcos_free(m_pool->header);
        m_pool->header = nullptr;
      }

      if (m_pool->queue)
      {
        mmal_queue_destroy(m_pool->queue);
        m_pool->queue = nullptr;
      }

      vcos_free(m_pool);
      m_pool = nullptr;
    }
    m_port->userdata = nullptr;
    m_port = nullptr;
  }
}

CVideoBuffer* CVideoBufferPoolMMAL::Get()
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  if (m_free.empty())
    return nullptr;

  CVideoBufferMMAL* buffer = nullptr;
  int id = m_free.front();
  m_free.pop_front();
  m_used.push_back(id);

  buffer = m_all[id];

  if (buffer)
    buffer->Acquire(GetPtr());

  return buffer;
}

CVideoBuffer* CVideoBufferPoolMMAL::Get(bool rendered)
{
  if (rendered)
  {
    std::unique_lock<CCriticalSection> lock(m_poolLock);
    if (m_ready.empty())
      return nullptr;

    CVideoBufferMMAL* buffer = nullptr;
    int id = m_ready.front();
    m_ready.pop_front();
    m_used.push_back(id);

    buffer = m_all[id];

    if (buffer)
      buffer->Acquire(GetPtr());
    return buffer;
  }
  else
    return Get();
}

bool CVideoBufferPoolMMAL::Move(AVFrame* frame, AVCodecID codecId, bool flushed, void* envPtr)
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  if (m_free.empty())
    return false;

  CVideoBufferMMAL* buffer = nullptr;
  int id = m_free.front();
  m_free.pop_front();
  m_ready.push_back(id);

  buffer = m_all[id];

  if (buffer)
    return buffer->UpdateBufferFromFrame(frame, codecId, flushed,
                                         envPtr ? (AVZcEnvPtr)envPtr : nullptr);

  return buffer != nullptr;
}

void CVideoBufferPoolMMAL::Put(CVideoBufferMMAL* buffer)
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  int id = buffer->GetId();
  auto it = m_free.begin();
  while (it != m_free.end())
  {
    if (*it == id)
    {
      m_free.erase(it);
      break;
    }
    else
      ++it;
  }
  m_ready.push_back(id);
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
  if (m_bufferManager && m_used.empty())
  {
    (m_bufferManager->*m_disposeCallback)(this);
    for (auto buffer : m_all)
      delete buffer;
    m_all.clear();
  }
}

void CVideoBufferPoolMMAL::Flush()
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  if (m_ready.empty())
    return;

  while (!m_ready.empty())
  {
    int id = m_ready.front();
    m_ready.pop_front();
    m_free.push_back(id);
    if (m_callback)
      m_callback(this, m_all[id], m_userdata);
  }
}

uint32_t CVideoBufferPoolMMAL::Length(bool rendered)
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  if (rendered)
    return m_ready.size();
  else
    return m_free.size();
}

void CVideoBufferPoolMMAL::Configure(AVPixelFormat format, int size)
{
  m_portFormat->encoding = TranslateFormat(format);
  m_portFormat->encoding_variant = 0;
  Configure(m_portFormat, nullptr, 0, size);
}

void CVideoBufferPoolMMAL::Configure(MMALFormat portFormat,
                                     VideoPicture* pBasePicture,
                                     uint32_t count,
                                     int32_t size)
{
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
        m_port->userdata = (struct MMAL_PORT_USERDATA_T*)this;
        m_port->buffer_num = 0;
        m_port->buffer_size = size;
        m_port->format->encoding = portFormat->encoding;
        m_port->format->encoding_variant = portFormat->encoding_variant;
        m_portFormat = mmal_format_alloc();
        m_portFormat->type = MMAL_ES_TYPE_VIDEO;
        mmal_port_parameter_set_uint32(m_port, MMAL_PARAMETER_EXTRA_BUFFERS, 0);
        mmal_port_parameter_set_boolean(m_port, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
        if (mmal_port_format_commit(m_port) == MMAL_SUCCESS)
        {
          mmal_format_full_copy(m_portFormat, m_port->format);
          m_portFormat->encoding = MMAL_ENCODING_UNKNOWN;
          m_portFormat->encoding_variant = MMAL_ENCODING_UNKNOWN;
          m_portFormat->extradata = nullptr;
          m_portFormat->extradata_size = 0;
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
  if (mmal_format_compare(m_portFormat, portFormat) != 0)
  {
    if (mmal_format_full_copy(m_portFormat, portFormat) != MMAL_SUCCESS)
    {
      CLog::Log(LOGERROR, "CVideoBufferPoolMMAL::{} - failed to copy port format", __FUNCTION__);
      return;
    }
  }

  if (count > 0)
  {
    if (!m_pool)
    {
      if ((m_pool = mmal_port_pool_create(m_port, count, size)) == NULL)
      {
        CLog::Log(LOGERROR, "CVideoBufferPoolMMAL::{} - failed to create pool", __FUNCTION__);
        return;
      }
      m_size = size;
      mmal_pool_callback_set(m_pool, CVideoBufferPoolMMAL::ProcessBufferCallback, (void*)this);
    }
    else if (m_size != size)
    {
      if (mmal_pool_resize(m_pool, count, size) != MMAL_SUCCESS)
      {
        CLog::Log(LOGERROR, "CVideoBufferPoolMMAL::{} - failed to resize pool", __FUNCTION__);
        return;
      }
      m_size = size;
    }
    InitializeBuffers(pBasePicture);
    if (m_port->is_enabled == 0)
      mmal_port_enable(m_port, NULL);
    if (m_component->is_enabled == 0)
      mmal_component_enable(m_component);
  }
}

void CVideoBufferPoolMMAL::InitializeBuffers(VideoPicture* pBasePicture)
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  MMALBufferHeader header = nullptr;
  while ((header = mmal_queue_get(m_pool->queue)) != NULL)
  {
    int index = m_all.size();
    CVideoBufferMMAL* buffer = new CVideoBufferMMAL(index, header);
    m_all.push_back(buffer);
    m_free.push_back(index);

    if (pBasePicture)
      buffer->SetBasePicture(pBasePicture);

    if (m_callback)
      m_callback(this, buffer, m_userdata);
  }
  lock.unlock();

  for (auto buffer : m_all)
    buffer->SetPortFormat(m_portFormat);
}

void CVideoBufferPoolMMAL::SetReleaseCallback(IVideoBufferPoolMMALCallback callback, void* userdata)
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  m_callback = callback;
  m_userdata = userdata;
}

bool CVideoBufferPoolMMAL::IsConfigured()
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  return m_component != nullptr && m_size != -1 && m_pool != nullptr &&
         m_portFormat->encoding != MMAL_ENCODING_UNKNOWN;
}

bool CVideoBufferPoolMMAL::IsCompatible(AVPixelFormat format, int size)
{
  std::unique_lock<CCriticalSection> lock(m_poolLock);
  if (m_component != nullptr && m_portFormat->encoding != TranslateFormat(format) && size != m_size)
    return false;

  return true;
}

void CVideoBufferPoolMMAL::Released(CVideoBufferManager& videoBufferManager)
{
  //CLog::Log(LOGDEBUG, "CVideoBufferPoolMMAL::{} - released pool", __FUNCTION__);
  videoBufferManager.RegisterPool(std::make_shared<CVideoBufferPoolMMAL>());
}

void CVideoBufferPoolMMAL::Discard(CVideoBufferManager* bm, ReadyToDispose cb)
{
  //CLog::Log(LOGDEBUG, "CVideoBufferPoolMMAL::{} - discarding pool", __FUNCTION__);
  if (m_used.empty())
  {
    (bm->*cb)(this);
    for (auto buffer : m_all)
      delete buffer;
    m_all.clear();
  }
  else
  {
    m_bufferManager = bm;
    m_disposeCallback = cb;
  }
}
