/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "settings/Settings.h"
#include "threads/CriticalSection.h"
#include "threads/SystemClock.h"
#include "windowing/GraphicContext.h"
#include "windowing/WinSystem.h"
#include "windowing/dmx/DmxUtils.h"

#include "platform/linux/input/LibInputHandler.h"

#include <utility>

class IDispResource;

namespace KODI
{
namespace WINDOWING
{
namespace DMX
{

class CWinSystemDmx : public CWinSystemBase
{
public:
  CWinSystemDmx();
  ~CWinSystemDmx() override = default;

  const std::string GetName() override { return "dmx"; }

  bool InitWindowSystem() override;
  bool DestroyWindowSystem() override;

  bool ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop) override;
  bool SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays) override;

  bool DisplayHardwareScalingEnabled() override;
  void UpdateDisplayHardwareScaling(const RESOLUTION_INFO& resInfo) override;

  bool CanDoWindowed() override { return false; }
  void UpdateResolutions() override;

  bool UseLimitedColor() override;

  bool Hide() override;
  bool Show(bool raise = true) override;

  void Register(IDispResource* resource) override;
  void Unregister(IDispResource* resource) override;
  int NoOfBuffers() override { return 2; }

  std::unique_ptr<CVideoSync> GetVideoSync(void* clock) override;

  void SetScalingGovernor(const char* governor) { m_DMX->SetScalingGovernor(governor); }
  uint64_t WaitVerticalSync(uint64_t sequence, uint32_t wait_ms = 0)
  {
    return m_DMX->WaitVerticalSync(sequence, nullptr, wait_ms);
  }
  uint64_t WaitVerticalSync(uint64_t sequence, uint64_t* time, uint32_t wait_ms = 0)
  {
    return m_DMX->WaitVerticalSync(sequence, time, wait_ms);
  }
  bool ReadPixels(int32_t x,
                  int32_t y,
                  int32_t width,
                  int32_t height,
                  uint32_t format,
                  bool swap,
                  void* pixels);

protected:
  void OnLostDevice();

  std::unique_ptr<CDmxUtils> m_DMX;

  CCriticalSection m_resourceSection;
  std::vector<IDispResource*> m_resources;

  bool m_dispReset;
  XbmcThreads::EndTime<> m_dispResetTimer;

  bool m_visible;

  std::shared_ptr<CSettings> m_settings;
  std::unique_ptr<CLibInputHandler> m_libinput;
};

} // namespace DMX
} // namespace WINDOWING
} // namespace KODI
