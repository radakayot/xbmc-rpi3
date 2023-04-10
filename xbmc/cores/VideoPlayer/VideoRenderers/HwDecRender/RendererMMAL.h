/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/VideoPlayer/Buffers/VideoBufferMMAL.h"
#include "cores/VideoPlayer/VideoRenderers/BaseRenderer.h"
#include "windowing/dmx/WinSystemDmx.h"

#include <atomic>

#include <interface/mmal/mmal.h>
#include <interface/mmal/util/mmal_connection.h>

#define MMAL_RENDERER_NUM_BUFFERS 6

class CVideoBuffer;
namespace MMAL
{
enum MMALRendererState
{
  MRS_UNINITIALIZED = 0, // 0
  MRS_INITIALIZED, // 1 x
  MRS_CONFIGURED, // 2 x
  MRS_RENDERING, // 3
  MRS_FLUSHING, // 4 x
  MRS_FLUSHED, // 5 x
  MRS_RESET,
  MRS_DESTROYING,
  MRS_ERROR, // 8
};

class CRendererMMAL : public CBaseRenderer
{
public:
  CRendererMMAL(KODI::WINDOWING::DMX::CWinSystemDmx* winSystem);
  ~CRendererMMAL() override;

  // Registration
  static CBaseRenderer* Create(CVideoBuffer* buffer);
  static void Register();

  // Player functions
  bool Configure(const VideoPicture& picture, float fps, unsigned int orientation) override;
  bool IsConfigured() override;
  void AddVideoPicture(const VideoPicture& picture, int index) override;
  void UnInit() override{};
  bool Flush(bool saveBuffers) override;
  void ReleaseBuffer(int idx) override;
  bool NeedBuffer(int idx) override;
  bool IsGuiLayer() override { return false; }
  CRenderInfo GetRenderInfo() override;
  void Update() override;
  void RenderUpdate(
      int index, int index2, bool clear, unsigned int flags, unsigned int alpha) override;
  bool RenderCapture(CRenderCapture* capture) override { return true; };
  bool ConfigChanged(const VideoPicture& picture) override;
  void SetBufferSize(int numBuffers) override;

  // Feature support
  bool SupportsMultiPassRendering() override { return false; };
  bool Supports(ERENDERFEATURE feature) const override;
  bool Supports(ESCALINGMETHOD method) const override;

protected:
  void ManageRenderArea() override;

private:
  static void ProcessControlCallback(MMALPort port, MMALBufferHeader header);
  static void ProcessInputCallback(MMALPort port, MMALBufferHeader header);

  bool ConfigurePort(MMALFormat format, uint32_t bufferSize);

  void AcquireBuffer(CVideoBufferMMAL* buffer, int index);
  bool SendBuffer(int index);

  std::atomic<MMALRendererState> m_state{MRS_UNINITIALIZED};

  MMALComponent m_renderer{nullptr};
  MMALPort m_port{nullptr};
  MMALFormat m_portFormat{nullptr};
  MMAL_CONNECTION_T* m_connection{nullptr};

  MMALComponent m_isp{nullptr};

  CVideoBufferMMAL* m_buffers[MMAL_RENDERER_NUM_BUFFERS];
  uint32_t m_bufferCount{MMAL_RENDERER_NUM_BUFFERS};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
  MMAL_DISPLAYREGION_T m_displayRegion{NULL};
#pragma GCC diagnostic pop

  KODI::WINDOWING::DMX::CWinSystemDmx* m_winSystem{nullptr};

  uint32_t m_renderFormats[24]{0};
  uint32_t m_ispFormats[64]{0};

  CCriticalSection m_portLock;
  CCriticalSection m_bufferLock;
  XbmcThreads::ConditionVariable m_bufferCondition;
};
} // namespace MMAL
