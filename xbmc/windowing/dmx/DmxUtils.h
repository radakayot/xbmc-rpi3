/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "threads/Condition.h"
#include "threads/CriticalSection.h"
#include "utils/EGLUtils.h"
#include "windowing/Resolution.h"

#include <memory>
#include <queue>

extern "C"
{
#include <interface/vchiq_arm/vchiq_if.h>
#include <interface/vctypes/vc_image_types.h>
#include <interface/vmcs_host/vc_cecservice.h>
#include <interface/vmcs_host/vc_dispmanx_types.h>
#include <interface/vmcs_host/vc_hdmi.h>
#include <interface/vmcs_host/vc_tvservice.h>
#include <interface/vmcs_host/vc_tvservice_defs.h>
#include <interface/vmcs_host/vc_vchi_gencmd.h>
}

namespace KODI
{
namespace WINDOWING
{
namespace DMX
{

/**
 * @brief A wrapper for DispmanX C classes.
 *
 */
class CDmxUtils
{
public:
  CDmxUtils(const CDmxUtils&) = delete;
  CDmxUtils& operator=(const CDmxUtils&) = delete;

  CDmxUtils();
  ~CDmxUtils() = default;

  bool Initialize();
  void Deinitialize();

  bool OpenDisplay();
  void CloseDisplay();

  const RESOLUTION_INFO& GetCurrentResolution(bool probe = false);
  std::vector<RESOLUTION_INFO> GetSupportedResolutions(bool probe = false);

  bool SetResolution(const RESOLUTION_INFO& res);
  bool IsCurrentResolution(const RESOLUTION_INFO& res);

  bool CreateSurface(EGLSurface surface, RESOLUTION_INFO& res);
  void DestroySurface();

  bool SetVisibility(bool visible);
  bool ResizeWindow(int32_t width, int32_t height);
  bool ResizeSurface(int32_t width, int32_t height);

  void BlankFrameBuffer(bool blank);

  bool ReadPixels(uint32_t x,
                  uint32_t y,
                  uint32_t width,
                  uint32_t height,
                  VC_IMAGE_TYPE_T format,
                  DISPMANX_TRANSFORM_T transform,
                  void* pixels,
                  uint32_t pitch);

  uint64_t WaitVerticalSync(uint64_t sequence, uint64_t* time, uint32_t wait_ms);
  void SetScalingGovernor(const char* governor);

protected:
  bool m_initialized{false};

private:
  static void VerticalSyncCallback(DISPMANX_UPDATE_HANDLE_T u, void* arg);
  static void VerticalSyncThreadCallback(DISPMANX_UPDATE_HANDLE_T u, void* arg);

  bool GetHdmiProperty(HDMI_PROPERTY_T property, uint32_t* param1, uint32_t* param2);
  bool SetHdmiProperty(HDMI_PROPERTY_T property, uint32_t param1, uint32_t param2);

  HDMI_PIXEL_CLOCK_TYPE_T GetHdmiPixelClock(float refresh_rate);
  float GetHdmiPixelRatio(uint32_t aspect_type, uint32_t width, uint32_t height);
  bool GetHdmiResolution(TV_SUPPORTED_MODE_NEW_T* mode, RESOLUTION_INFO& res);

  int GetHdmiModes(HDMI_RES_GROUP_T group, TV_SUPPORTED_MODE_NEW_T** modes);
  void ResolveHdmiModes(TV_SUPPORTED_MODE_NEW_T* modes, uint32_t count);

  CCriticalSection m_updateLock;

  CCriticalSection m_vsyncLock;
  uint64_t m_vsyncCount;
  struct timespec m_vsyncTime;
  XbmcThreads::ConditionVariable m_vsyncCondition;

  VCHI_INSTANCE_T m_vchi{nullptr};
  VCHI_CONNECTION_T* m_connections{nullptr};
  int32_t m_layer{1};
  std::vector<RESOLUTION_INFO> m_resolutions;
  RESOLUTION_INFO m_currentResolution;

  DISPMANX_MODEINFO_T m_displayInfo;
  VC_RECT_T m_sourceRectangle;
  VC_RECT_T m_screenRectangle;

  DISPMANX_DISPLAY_HANDLE_T m_display{DISPMANX_NO_HANDLE};
  DISPMANX_ELEMENT_HANDLE_T m_element{DISPMANX_NO_HANDLE};
};

} // namespace DMX
} // namespace WINDOWING
} // namespace KODI
