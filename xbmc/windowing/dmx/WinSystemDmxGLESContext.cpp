/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinSystemDmxGLESContext.h"

#include "application/Application.h"
#include "application/ApplicationPlayer.h"
#include "application/ApplicationPowerHandling.h"
#include "cores/DataCacheCore.h"
#include "cores/RetroPlayer/process/dmx/RPProcessInfoDmx.h"
#include "cores/RetroPlayer/rendering/VideoRenderers/RPRendererOpenGLES.h"
#include "cores/VideoPlayer/DVDCodecs/DVDFactoryCodec.h"
//#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodecFFmpegMMAL.h"
#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodecMMAL.h"
#include "cores/VideoPlayer/Process/dmx/ProcessInfoDmx.h"
#include "cores/VideoPlayer/VideoRenderers/HwDecRender/RendererMMAL.h"
#include "cores/VideoPlayer/VideoRenderers/LinuxRendererGLES.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFactory.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIDialog.h"
#include "guilib/GUIWindowManager.h"
#include "rendering/gles/ScreenshotSurfaceGLES.h"
#include "utils/BufferObjectFactory.h"
#include "utils/UDMABufferObject.h"
#include "utils/XTimeUtils.h"
#include "utils/log.h"
#include "windowing/WindowSystemFactory.h"

using namespace KODI::WINDOWING::DMX;

using namespace std::chrono_literals;

void CWinSystemDmxGLESContext::Register()
{
  CWindowSystemFactory::RegisterWindowSystem(CreateWinSystem, "dmx");
}

std::unique_ptr<CWinSystemBase> CWinSystemDmxGLESContext::CreateWinSystem()
{
  return std::make_unique<CWinSystemDmxGLESContext>();
}

bool CWinSystemDmxGLESContext::InitWindowSystem()
{
  if (!CWinSystemDmxEGLContext::InitWindowSystemEGL(EGL_OPENGL_ES2_BIT, EGL_OPENGL_ES_API))
  {
    return false;
  }
  RETRO::CRPProcessInfoDmx::Register();
  RETRO::CRPProcessInfoDmx::RegisterRendererFactory(new RETRO::CRendererFactoryOpenGLES);
  CDVDFactoryCodec::ClearHWAccels();
  MMAL::CDVDVideoCodecMMAL::Register();
  //MMAL::CDVDVideoCodecFFmpegMMAL::Register();
  VIDEOPLAYER::CRendererFactory::ClearRenderer();
  CLinuxRendererGLES::Register();
  MMAL::CRendererMMAL::Register();
  VIDEOPLAYER::CProcessInfoDmx::Register();

  CScreenshotSurfaceGLES::Register();

  return true;
}

bool CWinSystemDmxGLESContext::SetFullScreen(bool fullScreen,
                                             RESOLUTION_INFO& res,
                                             bool blankOtherDisplays)
{
  if (res.iWidth != m_nWidth || res.iHeight != m_nHeight)
  {
    CLog::Log(LOGDEBUG, "CWinSystemDmxGLESContext::{} - resolution changed, creating a new window",
              __FUNCTION__);
    CreateNewWindow("", fullScreen, res);
  }

  if (!m_eglContext.TrySwapBuffers())
  {
    CEGLUtils::Log(LOGERROR, "eglSwapBuffers failed");
    throw std::runtime_error("eglSwapBuffers failed");
  }

  if (CWinSystemDmx::SetFullScreen(fullScreen, res, blankOtherDisplays))
  {
    return CRenderSystemGLES::ResetRenderSystem(res.iWidth, res.iHeight);
  }
}

bool CWinSystemDmxGLESContext::ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop)
{
  if (CWinSystemDmx::ResizeWindow(newWidth, newHeight, newLeft, newTop))
  {
    return CRenderSystemGLES::ResetRenderSystem(newWidth, newHeight);
  }
  return false;
}

void CWinSystemDmxGLESContext::PresentRender(bool rendered, bool videoLayer)
{
  if (!m_bVsyncInit)
    SetVSync(true);

  if (!m_bRenderCreated)
    return;

  if (rendered || videoLayer)
  {
    if (rendered)
    {
      if (!m_eglContext.TrySwapBuffers())
      {
        CEGLUtils::Log(LOGERROR, "eglSwapBuffers failed");
        throw std::runtime_error("eglSwapBuffers failed");
      }
      if (!m_visible && CServiceBroker::GetGUI()->GetWindowManager().HasVisibleControls())
      {
        if (m_DMX->SetVisibility(true))
          m_visible = true;
      }
    }
    else if (videoLayer && !CServiceBroker::GetGUI()->GetWindowManager().HasVisibleControls())
    {
      if (!m_visible)
        KODI::TIME::Sleep(10ms);
      else if (m_DMX->SetVisibility(false))
        m_visible = false;
    }

    if (m_dispReset && m_dispResetTimer.IsTimePast())
    {
      CLog::Log(LOGDEBUG, "CWinSystemDmxGLESContext::{} - sending display reset to all clients",
                __FUNCTION__);
      m_dispReset = false;
      std::unique_lock<CCriticalSection> lock(m_resourceSection);

      for (auto resource : m_resources)
        resource->OnResetDisplay();
    }
  }
  else
  {
    KODI::TIME::Sleep(10ms);
  }
}

bool CWinSystemDmxGLESContext::CreateContext()
{
  CEGLAttributesVec contextAttribs;
  contextAttribs.Add({{EGL_CONTEXT_CLIENT_VERSION, 2}});

  if (!m_eglContext.CreateContext(contextAttribs))
  {
    CLog::Log(LOGERROR, "EGL context creation failed");
    return false;
  }
  return true;
}

void CWinSystemDmxGLESContext::SetVSyncImpl(bool enable)
{
  if (!m_eglContext.SetVSync(enable))
    CLog::Log(LOGERROR, "Could not set egl vsync");
}
