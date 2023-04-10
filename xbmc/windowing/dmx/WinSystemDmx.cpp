/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinSystemDmx.h"

#include "DmxDPMSSupport.h"
#include "OptionalsReg.h"
#include "ServiceBroker.h"
#include "messaging/ApplicationMessenger.h"
#include "settings/DisplaySettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "settings/lib/Setting.h"
#include "threads/CriticalSection.h"
#include "threads/SingleLock.h"
#include "utils/StringUtils.h"
#include "utils/log.h"
#include "windowing/dmx/VideoSyncDmx.h"

#include <float.h>
#include <mutex>
#include <string.h>

#include "system_gl.h"

using namespace KODI::WINDOWING::DMX;
using namespace std::chrono_literals;

CWinSystemDmx::CWinSystemDmx() : m_DMX(new CDmxUtils), m_libinput(new CLibInputHandler)
{
  if (!m_DMX->Initialize())
    throw std::runtime_error("Failed to initialize DMX!");

  m_dpms = std::make_shared<CDmxDPMSSupport>();
  m_libinput->Start();
  m_dispReset = false;
  m_visible = false;
}

bool CWinSystemDmx::InitWindowSystem()
{
  if (!CServiceBroker::GetSettingsComponent())
    return false;

  m_settings = CServiceBroker::GetSettingsComponent()->GetSettings();
  if (!m_settings)
    return false;

  auto setting = m_settings->GetSetting(CSettings::SETTING_VIDEOSCREEN_LIMITEDRANGE);
  if (setting)
    setting->SetVisible(true);

  setting = m_settings->GetSetting("videoscreen.limitguisize");
  if (setting)
    setting->SetVisible(true);

  CLog::Log(LOGDEBUG, "CWinSystemDmx::{} - initialized DMX", __FUNCTION__);
  if (CWinSystemBase::InitWindowSystem())
  {
    m_DMX->BlankFrameBuffer(true);
    return true;
  }
  return false;
}

bool CWinSystemDmx::DestroyWindowSystem()
{
  CLog::Log(LOGDEBUG, "CWinSystemDmx::{} - deinitialized DMX", __FUNCTION__);
  DestroyWindow();
  m_DMX->BlankFrameBuffer(false);
  m_libinput.reset();
  m_DMX->Deinitialize();
  return true;
}

void CWinSystemDmx::UpdateResolutions()
{
  auto resolutions = m_DMX->GetSupportedResolutions(true);
  if (resolutions.empty())
  {
    CLog::Log(LOGWARNING, "CWinSystemDmx::{} - Failed to get resolutions", __FUNCTION__);
  }
  else
  {
    const auto& current = m_DMX->GetCurrentResolution();

    CDisplaySettings::GetInstance().ClearCustomResolutions();
    CDisplaySettings::GetInstance().GetResolutionInfo(RES_DESKTOP) = current;
    for (auto& res : resolutions)
    {
      CServiceBroker::GetWinSystem()->GetGfxContext().ResetOverscan(res);
      CDisplaySettings::GetInstance().AddResolutionInfo(res);

      if (current.iScreenWidth == res.iScreenWidth && current.iScreenHeight == res.iScreenHeight &&
          current.iWidth == res.iWidth && current.iHeight == res.iHeight &&
          fabs(current.fRefreshRate - res.fRefreshRate) < FLT_EPSILON &&
          (current.dwFlags & D3DPRESENTFLAG_MODEMASK) == (res.dwFlags & D3DPRESENTFLAG_MODEMASK))
      {
        CDisplaySettings::GetInstance().GetResolutionInfo(RES_DESKTOP) = res;
      }
      CLog::Log(LOGINFO, "Found resolution {}x{} with {}x{}{} @ {:f} Hz", res.iWidth, res.iHeight,
                res.iScreenWidth, res.iScreenHeight,
                res.dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "", res.fRefreshRate);
    }
  }

  CDisplaySettings::GetInstance().ApplyCalibrations();
}

bool CWinSystemDmx::ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop)
{
  if (m_DMX->ResizeWindow(newWidth, newHeight))
  {
    m_nWidth = newWidth;
    m_nHeight = newHeight;
    return true;
  }

  return false;
}

bool CWinSystemDmx::SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays)
{
  if (!m_DMX->IsCurrentResolution(res))
  {
    OnLostDevice();

    if (!m_DMX->SetResolution(res))
    {
      CLog::Log(LOGERROR, "CWinSystemDmx::{} - failed to set HDMI mode", __FUNCTION__);
      return false;
    }

    auto delay =
        std::chrono::milliseconds(CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(
                                      "videoscreen.delayrefreshchange") *
                                  100);
    if (delay > 0ms)
      m_dispResetTimer.Set(delay);
  }
  int newWidth = res.iWidth;
  int newHeight = res.iHeight;
  if (fullScreen)
  {
    newWidth = res.iScreenWidth;
    newHeight = res.iScreenHeight;
  }
  if (m_DMX->ResizeWindow(newWidth, newHeight))
  {
    m_nWidth = newWidth;
    m_nHeight = newHeight;
    m_bFullScreen = fullScreen;
    if (res.iWidth > 0 && res.iHeight > 0)
      return m_DMX->ResizeSurface(res.iWidth, res.iHeight);
    else
      return m_DMX->ResizeSurface(newWidth, newHeight);
  }

  return false;
}

bool CWinSystemDmx::DisplayHardwareScalingEnabled()
{
  return true;
}

void CWinSystemDmx::UpdateDisplayHardwareScaling(const RESOLUTION_INFO& resInfo)
{
  m_DMX->ResizeSurface(resInfo.iWidth, resInfo.iHeight);
}

bool CWinSystemDmx::UseLimitedColor()
{
  return m_settings->GetBool(CSettings::SETTING_VIDEOSCREEN_LIMITEDRANGE);
}

bool CWinSystemDmx::Hide()
{
  if (!m_visible)
    return true;
  else if (!m_DMX->SetVisibility(false))
    return false;

  m_visible = false;
  return true;
}

bool CWinSystemDmx::Show(bool raise)
{
  if (m_visible)
    return true;
  else if (!m_DMX->SetVisibility(true))
    return false;

  m_visible = true;
  return true;
}

void CWinSystemDmx::Register(IDispResource* resource)
{
  std::unique_lock<CCriticalSection> lock(m_resourceSection);
  m_resources.push_back(resource);
}

void CWinSystemDmx::Unregister(IDispResource* resource)
{
  std::unique_lock<CCriticalSection> lock(m_resourceSection);
  std::vector<IDispResource*>::iterator i = find(m_resources.begin(), m_resources.end(), resource);
  if (i != m_resources.end())
  {
    m_resources.erase(i);
  }
}

bool CWinSystemDmx::ReadPixels(
    int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format, bool swap, void* pixels)
{
  VC_IMAGE_TYPE_T imgType = VC_IMAGE_1BPP;
  uint32_t pitch = width;

  if (format == GL_RGBA)
  {
    imgType = VC_IMAGE_RGBA32;
    pitch *= 4;
  }
  else if (format == GL_RGB565)
  {
    imgType = VC_IMAGE_RGB565;
    pitch *= 2;
  }
  DISPMANX_TRANSFORM_T transform = DISPMANX_NO_ROTATE;

  if (swap)
    transform = static_cast<DISPMANX_TRANSFORM_T>(transform | DISPMANX_SNAPSHOT_SWAP_RED_BLUE);

  return m_DMX->ReadPixels(x, y, width, height, imgType, transform, pixels, pitch);
}

std::unique_ptr<CVideoSync> CWinSystemDmx::GetVideoSync(void* clock)
{
  return std::make_unique<CVideoSyncDmx>(clock);
}

void CWinSystemDmx::OnLostDevice()
{
  CLog::Log(LOGDEBUG, "CWinSystemDmx::{} - notify display change event", __FUNCTION__);
  m_dispReset = true;

  std::unique_lock<CCriticalSection> lock(m_resourceSection);
  for (auto resource : m_resources)
    resource->OnLostDisplay();
}