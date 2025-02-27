/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "WinSystemDmxEGLContext.h"
#include "rendering/gles/RenderSystemGLES.h"
#include "utils/EGLUtils.h"

#include <memory>

class CVaapiProxy;

namespace KODI
{
namespace WINDOWING
{
namespace DMX
{

class CWinSystemDmxGLESContext : public CWinSystemDmxEGLContext, public CRenderSystemGLES
{
public:
  CWinSystemDmxGLESContext() = default;
  ~CWinSystemDmxGLESContext() override = default;

  static void Register();
  static std::unique_ptr<CWinSystemBase> CreateWinSystem();

  // Implementation of CWinSystemBase via CWinSystemDmx
  CRenderSystemBase* GetRenderSystem() override { return this; }

  bool InitWindowSystem() override;
  bool ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop) override;
  bool SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays) override;
  void PresentRender(bool rendered, bool videoLayer) override;

protected:
  void SetVSyncImpl(bool enable) override;
  void PresentRenderImpl(bool rendered) override{};
  bool CreateContext() override;

private:
  uint64_t m_sequence{0};
};

} // namespace DMX
} // namespace WINDOWING
} // namespace KODI
