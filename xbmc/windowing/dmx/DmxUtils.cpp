/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DmxUtils.h"

#include "threads/SingleLock.h"
#include "utils/StringUtils.h"
#include "utils/TimeUtils.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"

#include <float.h>
#include <fstream>
#include <mutex>

#include <interface/vcsm/user-vcsm.h>
#include <sys/ioctl.h>

using namespace KODI::WINDOWING::DMX;

namespace
{
std::once_flag flag;
}

#define DISPMANX_ID_DEFAULT DISPMANX_ID_HDMI

CDmxUtils::CDmxUtils()
{
  m_initialized = false;
}

bool CDmxUtils::Initialize()
{
  if (m_initialized)
    return true;

  if (vcos_init() == VCOS_SUCCESS)
  {
    if (vcsm_init_ex(0, -1) == VCOS_SUCCESS)
    {
      if (vchi_initialise(&m_vchi) == VCOS_SUCCESS)
      {
        m_connections =
            (VCHI_CONNECTION_T*)vcos_malloc(sizeof(VCHI_CONNECTION_T), "vchi connections");
        if (vchi_connect(&m_connections, 1, m_vchi) == VCOS_SUCCESS)
        {
          if (vc_vchi_tv_init(m_vchi, &m_connections, 1) == VCOS_SUCCESS)
          {
            vc_vchi_dispmanx_init(m_vchi, &m_connections, 1);
            m_initialized = true;
          }
          else
            CLog::Log(LOGERROR, "CDmxUtils::{} - unable to initialize tv service", __FUNCTION__);
        }
        else
          CLog::Log(LOGERROR, "CDmxUtils::{} - unable to connect vchi", __FUNCTION__);
      }
      else
        CLog::Log(LOGERROR, "CDmxUtils::{} - unable to initialize vchi", __FUNCTION__);
    }
    else
      CLog::Log(LOGERROR, "CDmxUtils::{} - unable to initialize vcsm", __FUNCTION__);
  }
  else
    CLog::Log(LOGERROR, "CDmxUtils::{} - unable to initialize vcos", __FUNCTION__);

  if (!m_initialized)
  {
    Deinitialize();
    return false;
  }
  return true;
}

void CDmxUtils::Deinitialize()
{
  if (m_initialized)
  {
    CloseDisplay();
    vc_dispmanx_stop();
    vc_vchi_tv_stop();
    if (m_vchi)
    {
      if (vchi_disconnect(m_vchi) == VCOS_SUCCESS)
        m_vchi = nullptr;
    }
    if (m_connections)
    {
      vcos_free(m_connections);
      m_connections = nullptr;
    }
    vcos_deinit();
    vcsm_exit();
  }
}

bool CDmxUtils::OpenDisplay()
{
  if (m_display != DISPMANX_NO_HANDLE)
    return true;

  std::unique_lock<CCriticalSection> lock(m_updateLock);
  m_display = vc_dispmanx_display_open(0);
  if (m_display != (unsigned)DISPMANX_INVALID &&
      vc_dispmanx_vsync_callback(m_display, CDmxUtils::VerticalSyncCallback, (void*)this) ==
          DISPMANX_SUCCESS)
  {
    if (vc_dispmanx_display_get_info(m_display, &m_displayInfo) != DISPMANX_SUCCESS)
    {
      m_displayInfo.width = 0;
      m_displayInfo.height = 0;
    }
    return true;
  }
  else
  {
    m_display = DISPMANX_NO_HANDLE;
  }

  return false;
}

void CDmxUtils::CloseDisplay()
{
  if (m_display != DISPMANX_NO_HANDLE)
  {
    DestroySurface();
    std::unique_lock<CCriticalSection> lock(m_updateLock);
    vc_dispmanx_vsync_callback(m_display, NULL, NULL);
    vc_dispmanx_display_close(m_display);
    m_display = DISPMANX_NO_HANDLE;
  }
}

bool CDmxUtils::GetHdmiProperty(HDMI_PROPERTY_T property, uint32_t* param1, uint32_t* param2)
{

  HDMI_PROPERTY_PARAM_T p;
  memset(&p, 0, sizeof(HDMI_PROPERTY_PARAM_T));
  p.property = property;
  if (vc_tv_hdmi_get_property(&p) == VCOS_SUCCESS)
  {
    if (param1)
      *param1 = p.param1;
    if (param2)
      *param2 = p.param2;
    return true;
  }
  return false;
}

bool CDmxUtils::SetHdmiProperty(HDMI_PROPERTY_T property, uint32_t param1, uint32_t param2)
{
  HDMI_PROPERTY_PARAM_T p;
  memset(&p, 0, sizeof(HDMI_PROPERTY_PARAM_T));
  p.property = property;
  p.param1 = param1;
  p.param2 = param2;
  return vc_tv_hdmi_set_property(&p) == VCOS_SUCCESS;
}

HDMI_PIXEL_CLOCK_TYPE_T CDmxUtils::GetHdmiPixelClock(float refresh_rate)
{
  int frame_rate = (int)(refresh_rate + 0.5f);
  if (fabsf(refresh_rate * (1001.0f / 1000.0f) - frame_rate) < fabsf(refresh_rate - frame_rate))
    return HDMI_PIXEL_CLOCK_TYPE_NTSC;
  else
    return HDMI_PIXEL_CLOCK_TYPE_PAL;
}

float CDmxUtils::GetHdmiPixelRatio(uint32_t aspect_type, uint32_t width, uint32_t height)
{
  float result = 1.0f;
  switch ((HDMI_ASPECT_T)aspect_type)
  {
    case HDMI_ASPECT_4_3:
      result = 4.0f / 3.0f;
      break;
    case HDMI_ASPECT_14_9:
      result = 14.0f / 9.0f;
      break;
    case HDMI_ASPECT_16_9:
      result = 16.0f / 9.0f;
      break;
    case HDMI_ASPECT_5_4:
      result = 5.0f / 4.0f;
      break;
    case HDMI_ASPECT_16_10:
      result = 16.0f / 10.0f;
      break;
    case HDMI_ASPECT_15_9:
      result = 15.0f / 9.0f;
      break;
    case HDMI_ASPECT_64_27:
      result = 64.0f / 27.0f;
      break;
    case HDMI_ASPECT_256_135:
      result = 256.0f / 135.0f;
      break;
    case HDMI_ASPECT_UNKNOWN:
    default:
      return result;
  }
  return result / ((float)width / (float)height);
}

bool CDmxUtils::GetHdmiResolution(TV_SUPPORTED_MODE_NEW_T* mode, RESOLUTION_INFO& res)
{
  if (mode->code == 0)
    return false;

  if (mode->code == HDMI_RES_GROUP_CEA && mode->struct_3d_mask != 0)
  {
    if (mode->struct_3d_mask & HDMI_3D_STRUCT_TOP_AND_BOTTOM)
      res.dwFlags = D3DPRESENTFLAG_MODE3DTB;
    else
      res.dwFlags = D3DPRESENTFLAG_MODE3DSBS;
  }
  else if (mode->scan_mode == 1)
    res.dwFlags = D3DPRESENTFLAG_INTERLACED;
  else
    res.dwFlags = D3DPRESENTFLAG_PROGRESSIVE;

  res.dwFlags |= ((mode->code) << 24) | ((mode->group) << 16);
  res.bFullScreen = true;
  res.fRefreshRate = (float)mode->frame_rate;
  res.iWidth = mode->width;
  res.iHeight = mode->height;
  res.iScreenWidth = mode->width;
  res.iScreenHeight = mode->height;
  res.fPixelRatio = GetHdmiPixelRatio(mode->aspect_ratio, mode->width, mode->height);
  res.iSubtitles = (int)(0.965f * (float)mode->height);
  res.strMode =
      StringUtils::Format("{}x{}{} @ {:.6f} Hz", res.iScreenWidth, res.iScreenHeight,
                          res.dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "", res.fRefreshRate);
  return true;
}

int CDmxUtils::GetHdmiModes(HDMI_RES_GROUP_T group, TV_SUPPORTED_MODE_NEW_T** modes)
{
  int max_count =
      vc_tv_hdmi_get_supported_modes_new_id(DISPMANX_ID_DEFAULT, group, NULL, 0, NULL, NULL);
  if (max_count > 0)
  {
    *modes = new TV_SUPPORTED_MODE_NEW_T[max_count];
    return vc_tv_hdmi_get_supported_modes_new_id(DISPMANX_ID_DEFAULT, group, *modes, max_count,
                                                 NULL, NULL);
  }
  return 0;
}

void CDmxUtils::ResolveHdmiModes(TV_SUPPORTED_MODE_NEW_T* modes, uint32_t count)
{
  TV_SUPPORTED_MODE_NEW_T* mode;
  mode = modes;
  for (uint32_t i = 0; i < count; i++, mode++)
  {
    RESOLUTION_INFO res;
    if (!GetHdmiResolution(mode, res))
      continue;

    bool found_res = false;
    for (auto& resolution : m_resolutions)
    {
      if (resolution.iScreenWidth == res.iScreenWidth &&
          resolution.iScreenHeight == res.iScreenHeight &&
          resolution.iWidth == res.iWidth && // width
          resolution.iHeight == res.iHeight && // height
          fabs(resolution.fRefreshRate - res.fRefreshRate) < FLT_EPSILON &&
          (resolution.dwFlags & D3DPRESENTFLAG_MODEMASK) == (res.dwFlags & D3DPRESENTFLAG_MODEMASK))
      {
        found_res = true;
        break;
      }
    }
    if (!found_res)
    {
      m_resolutions.push_back(res);
    }
  }
}

std::vector<RESOLUTION_INFO> CDmxUtils::GetSupportedResolutions(bool probe)
{
  if (probe)
  {
    TV_SUPPORTED_MODE_NEW_T* cea_modes;
    TV_SUPPORTED_MODE_NEW_T* dmt_modes;
    uint32_t cea_mode_count = GetHdmiModes(HDMI_RES_GROUP_CEA, &cea_modes);
    uint32_t dmt_mode_count = GetHdmiModes(HDMI_RES_GROUP_DMT, &dmt_modes);

    GetCurrentResolution(probe);

    m_resolutions.clear();
    m_resolutions.reserve(cea_mode_count + dmt_mode_count);

    ResolveHdmiModes(cea_modes, cea_mode_count);
    ResolveHdmiModes(dmt_modes, dmt_mode_count);

    if (cea_mode_count > 0)
      delete cea_modes;
    if (dmt_mode_count > 0)
      delete dmt_modes;
  }
  return m_resolutions;
}

const RESOLUTION_INFO& CDmxUtils::GetCurrentResolution(bool probe)
{
  if (probe)
  {
    TV_DISPLAY_STATE_T tv_state;
    memset(&tv_state, 0, sizeof(TV_DISPLAY_STATE_T));
    vc_tv_get_display_state_id(DISPMANX_ID_DEFAULT, &tv_state);

    if ((tv_state.state & VC_HDMI_HDMI) != 0)
    {
      RESOLUTION_INFO res;
      TV_SUPPORTED_MODE_NEW_T mode;

      memset(&mode, 0, sizeof(TV_SUPPORTED_MODE_NEW_T));
      mode.scan_mode = tv_state.display.hdmi.scan_mode;
      mode.code = tv_state.display.hdmi.mode;
      mode.width = tv_state.display.hdmi.width;
      mode.height = tv_state.display.hdmi.height;
      mode.frame_rate = tv_state.display.hdmi.frame_rate;
      mode.aspect_ratio = tv_state.display.hdmi.display_options.aspect;
      mode.struct_3d_mask = tv_state.display.hdmi.format_3d;
      if (GetHdmiResolution(&mode, res))
        m_currentResolution = res;
    }
  }
  return m_currentResolution;
}

static void vc_tv_hdmi_sync_callback(void* userdata,
                                     uint32_t reason,
                                     uint32_t param1,
                                     uint32_t param2)
{
  switch (reason)
  {
    case VC_HDMI_UNPLUGGED:
    case VC_HDMI_STANDBY:
      break;
    case VC_SDTV_NTSC:
    case VC_SDTV_PAL:
    case VC_HDMI_HDMI:
    case VC_HDMI_DVI:
      sem_post((sem_t*)userdata);
      break;
  }
}

bool CDmxUtils::SetResolution(const RESOLUTION_INFO& res)
{
  HDMI_RES_GROUP_T group = static_cast<HDMI_RES_GROUP_T>((res.dwFlags >> 16) & 0xff);
  uint32_t mode = (res.dwFlags >> 24) & 0xff;
  bool result = false;

  if (group != HDMI_RES_GROUP_INVALID && mode > 0)
  {
    sem_t hdmi_sync;
    SetHdmiProperty(HDMI_PROPERTY_PIXEL_CLOCK_TYPE, GetHdmiPixelClock(res.fRefreshRate), 0);

    sem_init(&hdmi_sync, 0, 0);
    vc_tv_register_callback(vc_tv_hdmi_sync_callback, &hdmi_sync);

    if (vc_tv_hdmi_power_on_explicit_new_id(DISPMANX_ID_HDMI, HDMI_MODE_HDMI, group, mode) == 0)
    {
      sem_wait(&hdmi_sync);
      m_currentResolution = res;
      result = true;
    }
    vc_tv_unregister_callback(vc_tv_hdmi_sync_callback);
    sem_destroy(&hdmi_sync);
  }
  return result;
}

bool CDmxUtils::IsCurrentResolution(const RESOLUTION_INFO& res)
{
  RESOLUTION_INFO current = GetCurrentResolution(true);
  return (current.iScreenWidth == res.iScreenWidth && current.iScreenHeight == res.iScreenHeight &&
          fabs(current.fRefreshRate - res.fRefreshRate) < FLT_EPSILON &&
          (current.dwFlags & D3DPRESENTFLAG_MODEMASK) == (res.dwFlags & D3DPRESENTFLAG_MODEMASK));
}

void CDmxUtils::BlankFrameBuffer(bool blank)
{
  std::ofstream m_output{"/sys/class/graphics/fb0/blank"};
  m_output << std::string(blank ? "1" : "0");
  m_output.close();
}

bool CDmxUtils::CreateSurface(EGLSurface surface, RESOLUTION_INFO& res)
{
  std::unique_lock<CCriticalSection> lock(m_updateLock);
  DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
  if (update != DISPMANX_NO_HANDLE)
  {
    VC_DISPMANX_ALPHA_T alpha;
    DISPMANX_CLAMP_T clamp;
    memset(&alpha, 0x0, sizeof(VC_DISPMANX_ALPHA_T));
    memset(&clamp, 0x0, sizeof(DISPMANX_CLAMP_T));

    m_layer = 1;

    m_screenRectangle.x = 0;
    m_screenRectangle.y = 0;
    m_screenRectangle.width = res.iScreenWidth;
    m_screenRectangle.height = res.iScreenHeight;

    m_sourceRectangle.x = 0;
    m_sourceRectangle.y = 0;
    m_sourceRectangle.width = res.iWidth << 16;
    m_sourceRectangle.height = res.iHeight << 16;

    alpha.flags = DISPMANX_FLAGS_ALPHA_FROM_SOURCE;

    m_element = vc_dispmanx_element_add(update, m_display,
                                        m_layer, // layer
                                        &m_screenRectangle,
                                        (DISPMANX_RESOURCE_HANDLE_T)0, // src
                                        &m_sourceRectangle,
                                        DISPMANX_PROTECTION_NONE, // protection mode
                                        &alpha, // alpha
                                        &clamp, // clamp
                                        DISPMANX_NO_ROTATE); // transform

    if (m_element != (unsigned)DISPMANX_INVALID)
    {
      EGL_DISPMANX_WINDOW_T* window = nullptr;
      memset(surface, 0, sizeof(EGL_DISPMANX_WINDOW_T));
      window = (EGL_DISPMANX_WINDOW_T*)surface;

      window->element = m_element;
      window->width = res.iWidth;
      window->height = res.iHeight;
      vc_dispmanx_display_set_background(update, m_display, 0x00, 0x00, 0x00);
      return vc_dispmanx_update_submit_sync(update) == DISPMANX_SUCCESS;
    }
    else
    {
      m_element = DISPMANX_NO_HANDLE;
    }
  }

  return m_element != DISPMANX_NO_HANDLE;
}

void CDmxUtils::DestroySurface()
{
  if (m_element != DISPMANX_NO_HANDLE)
  {
    std::unique_lock<CCriticalSection> lock(m_updateLock);
    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
    vc_dispmanx_element_remove(update, m_element);
    vc_dispmanx_update_submit_sync(update);
    m_element = DISPMANX_NO_HANDLE;
  }
}

bool CDmxUtils::SetVisibility(bool visible)
{
  if (m_element != DISPMANX_NO_HANDLE)
  {
    int32_t layer = visible ? 1 : -1;
    if (m_layer == layer)
      return true;

    std::unique_lock<CCriticalSection> lock(m_updateLock);
    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(VCOS_THREAD_PRI_ABOVE_NORMAL);
    if (update == DISPMANX_NO_HANDLE)
      return false;

    if (visible)
    {
      m_screenRectangle.x = 0;
      m_screenRectangle.y = 0;
    }
    else
    {
      m_screenRectangle.x = m_screenRectangle.width;
      m_screenRectangle.y = m_screenRectangle.height;
    }

    int32_t result = vc_dispmanx_element_change_attributes(
        update, m_element, 5, layer, 0, &m_screenRectangle, nullptr, 0, DISPMANX_NO_ROTATE);
    if (result == DISPMANX_SUCCESS)
      m_layer = layer;
    return vc_dispmanx_update_submit_sync(update) == DISPMANX_SUCCESS && result == DISPMANX_SUCCESS;
  }
  return false;
}

bool CDmxUtils::ResizeWindow(int32_t width, int32_t height)
{
  if (m_element != DISPMANX_NO_HANDLE)
  {
    if (m_screenRectangle.width == width && m_screenRectangle.height == height)
      return true;

    if (m_screenRectangle.x == m_screenRectangle.width &&
        m_screenRectangle.y == m_screenRectangle.height)
    {
      m_screenRectangle.x = width;
      m_screenRectangle.y = height;
    }

    m_screenRectangle.width = width;
    m_screenRectangle.height = height;

    std::unique_lock<CCriticalSection> lock(m_updateLock);
    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
    int32_t result = vc_dispmanx_element_change_attributes(
        update, m_element, 4, 0, 0, &m_screenRectangle, nullptr, 0, DISPMANX_NO_ROTATE);

    return vc_dispmanx_update_submit_sync(update) == DISPMANX_SUCCESS && result == DISPMANX_SUCCESS;
  }
  return false;
}

bool CDmxUtils::ResizeSurface(int32_t width, int32_t height)
{
  if (m_element != DISPMANX_NO_HANDLE)
  {
    m_sourceRectangle.x = 0;
    m_sourceRectangle.y = 0;

    m_sourceRectangle.width = width << 16;
    m_sourceRectangle.height = height << 16;

    std::unique_lock<CCriticalSection> lock(m_updateLock);
    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
    int32_t result = vc_dispmanx_element_change_attributes(
        update, m_element, 8, 0, 0, nullptr, &m_sourceRectangle, 0, DISPMANX_NO_ROTATE);

    return vc_dispmanx_update_submit_sync(update) == DISPMANX_SUCCESS && result == DISPMANX_SUCCESS;
  }
  return false;
}

bool CDmxUtils::ReadPixels(uint32_t x,
                           uint32_t y,
                           uint32_t width,
                           uint32_t height,
                           VC_IMAGE_TYPE_T format,
                           DISPMANX_TRANSFORM_T transform,
                           void* pixels,
                           uint32_t pitch)
{
  uint32_t flags = 0;

  if (m_display != DISPMANX_NO_HANDLE)
  {
    DISPMANX_RESOURCE_HANDLE_T resource;
    uint32_t unused;
    resource = vc_dispmanx_resource_create(format, width, height, &unused);

    if (resource != (unsigned)DISPMANX_INVALID)
    {
      bool result = false;
      if (vc_dispmanx_snapshot(m_display, resource, transform) == DISPMANX_SUCCESS)
      {
        VC_RECT_T rect;
        vc_dispmanx_rect_set(&rect, x, y, width, height);
        result = vc_dispmanx_resource_read_data(resource, &rect, pixels, pitch) == DISPMANX_SUCCESS;
      }
      vc_dispmanx_resource_delete(resource);
      return result;
    }
  }
  return false;
}

uint64_t CDmxUtils::WaitVerticalSync(uint64_t sequence, uint64_t* time, uint32_t wait_ms)
{
  std::unique_lock<CCriticalSection> vlock(m_vsyncLock);
  if (m_vsyncCount < sequence)
  {
    if (wait_ms > 0)
      m_vsyncCondition.wait(vlock, std::chrono::milliseconds(wait_ms));
    else
      while (m_vsyncCount < sequence)
        m_vsyncCondition.wait(vlock);
  }
  if (time)
    *time = ((int64_t)m_vsyncTime.tv_sec * 1000000000L) + m_vsyncTime.tv_nsec;
  return m_vsyncCount;
}

void CDmxUtils::VerticalSyncCallback(DISPMANX_UPDATE_HANDLE_T u, void* arg)
{
  CDmxUtils* dmx = static_cast<CDmxUtils*>(arg);
  if (dmx)
  {
    std::unique_lock<CCriticalSection> lock(dmx->m_vsyncLock);
    clock_gettime(CLOCK_MONOTONIC_RAW, &dmx->m_vsyncTime);
    dmx->m_vsyncCount++;
    lock.unlock();
    dmx->m_vsyncCondition.notifyAll();
  }
}
