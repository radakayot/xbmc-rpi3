/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "guilib/DispResource.h"
#include "windowing/VideoSync.h"
#include "windowing/dmx/WinSystemDmx.h"

#include <pthread.h>

class CVideoSyncDmx : public CVideoSync, IDispResource
{
public:
  explicit CVideoSyncDmx(void* clock) : CVideoSync(clock){};
  CVideoSyncDmx() = delete;
  ~CVideoSyncDmx() override = default;
  bool Setup(PUPDATECLOCK func) override;
  void Run(CEvent& stopEvent) override;
  void Cleanup() override;
  float GetFps() override;
  void OnResetDisplay() override;
  void RefreshChanged() override;

private:
  bool AdjustThreadPriority(pthread_t t);
  std::atomic<bool> m_abort{false};
  KODI::WINDOWING::DMX::CWinSystemDmx* m_winSystem{nullptr};
};