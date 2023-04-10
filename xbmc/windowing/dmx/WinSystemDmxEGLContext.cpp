/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinSystemDmxEGLContext.h"

#include "OptionalsReg.h"
#include "cores/VideoPlayer/DVDCodecs/DVDFactoryCodec.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFactory.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"

#include <float.h>

using namespace KODI::WINDOWING::DMX;

CWinSystemDmxEGLContext::CWinSystemDmxEGLContext() : CWinSystemDmx()
{
  m_eglDisplay = EGL_NO_DISPLAY;
  m_eglSurface = EGL_NO_SURFACE;
}

EGLDisplay CWinSystemDmxEGLContext::GetEGLDisplay() const
{
  return m_eglContext.GetEGLDisplay();
}

EGLSurface CWinSystemDmxEGLContext::GetEGLSurface() const
{
  return m_eglContext.GetEGLSurface();
}

EGLContext CWinSystemDmxEGLContext::GetEGLContext() const
{
  return m_eglContext.GetEGLContext();
}

EGLConfig CWinSystemDmxEGLContext::GetEGLConfig() const
{
  return m_eglContext.GetEGLConfig();
}

bool CWinSystemDmxEGLContext::InitWindowSystemEGL(EGLint renderableType, EGLint apiType)
{
  m_eglDisplay = EGL_DEFAULT_DISPLAY;

  if (!CWinSystemDmx::InitWindowSystem())
  {
    return false;
  }

  if (!m_eglContext.CreateDisplay(m_eglDisplay))
  {
    return false;
  }

  if (!m_eglContext.InitializeDisplay(apiType))
  {
    return false;
  }

  if (!m_eglContext.ChooseConfig(renderableType))
  {
    return false;
  }

  if (!CreateContext())
  {
    return false;
  }

  return true;
}

bool CWinSystemDmxEGLContext::CreateNewWindow(const std::string& name,
                                              bool fullScreen,
                                              RESOLUTION_INFO& res)
{
  if (!DestroyWindow())
  {
    return false;
  }

  if (!m_DMX->IsCurrentResolution(res))
  {
    //Notify other subsystems that we change resolution
    OnLostDevice();

    if (!m_DMX->SetResolution(res))
    {
      CLog::Log(LOGERROR, "CWinSystemDmxEGLContext::{} - failed to set mode", __FUNCTION__);
      return false;
    }
  }

  if (!m_DMX->OpenDisplay())
  {
    return false;
  }

  if (m_eglSurface == EGL_NO_SURFACE)
  {
    m_eglSurface = static_cast<EGLNativeWindowType>(new EGL_DISPMANX_WINDOW_T);
    if (!m_DMX->CreateSurface(m_eglSurface, res))
    {
      CLog::Log(LOGERROR, "CWinSystemDmxEGLContext::{} - failed to create dispmanx surface",
                __FUNCTION__);
      return false;
    }
  }

  if (!m_eglContext.CreateSurface(m_eglSurface))
  {
    CLog::Log(LOGERROR, "CWinSystemDmxEGLContext::{} - failed to create egl surface", __FUNCTION__);
    return false;
  }

  if (!m_eglContext.BindContext())
  {
    return false;
  }

  m_bFullScreen = fullScreen;
  m_nWidth = res.iWidth;
  m_nHeight = res.iHeight;
  m_fRefreshRate = res.fRefreshRate;
  m_bWindowCreated = true;

  return true;
}

bool CWinSystemDmxEGLContext::DestroyWindow()
{
  m_eglContext.DestroySurface();
  if (m_eglSurface != EGL_NO_SURFACE)
  {
    m_DMX->DestroySurface();
    m_eglSurface = EGL_NO_SURFACE;
    m_DMX->CloseDisplay();
  }
  m_bWindowCreated = false;
  return true;
}

bool CWinSystemDmxEGLContext::DestroyWindowSystem()
{
  CDVDFactoryCodec::ClearHWAccels();
  VIDEOPLAYER::CRendererFactory::ClearRenderer();
  m_eglContext.Destroy();

  return CWinSystemDmx::DestroyWindowSystem();
}