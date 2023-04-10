/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "WinSystemDmx.h"
#include "utils/EGLUtils.h"

#include <memory>

namespace KODI
{
namespace WINDOWING
{
namespace DMX
{

class CWinSystemDmxEGLContext : public CWinSystemDmx
{
public:
  CWinSystemDmxEGLContext();

  ~CWinSystemDmxEGLContext() override = default;

  bool DestroyWindowSystem() override;
  bool CreateNewWindow(const std::string& name, bool fullScreen, RESOLUTION_INFO& res) override;
  bool DestroyWindow() override;

  EGLDisplay GetEGLDisplay() const;
  EGLSurface GetEGLSurface() const;
  EGLContext GetEGLContext() const;
  EGLConfig GetEGLConfig() const;

protected:
  /**
   * Inheriting classes should override InitWindowSystem() without parameters
   * and call this function there with appropriate parameters
   */
  bool InitWindowSystemEGL(EGLint renderableType, EGLint apiType);
  virtual bool CreateContext() = 0;

  CEGLContextUtils m_eglContext;

  EGLDisplay m_eglDisplay;
  EGLSurface m_eglSurface;
};

} // namespace DMX
} // namespace WINDOWING
} // namespace KODI
