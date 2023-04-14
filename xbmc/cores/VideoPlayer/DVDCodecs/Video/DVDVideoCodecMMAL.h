/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/VideoPlayer/Buffers/VideoBuffer.h"
#include "cores/VideoPlayer/Buffers/VideoBufferMMAL.h"
#include "cores/VideoPlayer/Buffers/VideoBufferPoolMMAL.h"
#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodec.h"
#include "cores/VideoPlayer/DVDStreamInfo.h"
#include "threads/Event.h"

#include <atomic>
#include <memory>

#include <interface/mmal/mmal.h>

#define MMAL_CODEC_NUM_BUFFERS 4

namespace MMAL
{
enum MMALCodecState
{
  MCS_UNINITIALIZED = 0,
  MCS_INITIALIZED,
  MCS_OPENED,
  MCS_DECODING,
  MCS_FLUSHING,
  MCS_FLUSHED,
  MCS_CLOSING,
  MCS_CLOSED,
  MCS_RESET,
  MCS_ERROR,
};

class CDVDVideoCodecMMAL : public CDVDVideoCodec, public CThread
{
public:
  explicit CDVDVideoCodecMMAL(CProcessInfo& processInfo);
  ~CDVDVideoCodecMMAL() override;

  static std::unique_ptr<CDVDVideoCodec> CreateCodec(CProcessInfo& processInfo);
  static void Register();

  bool Open(CDVDStreamInfo& hints, CDVDCodecOptions& options) override;
  bool AddData(const DemuxPacket& packet) override;
  void Reset() override;
  CDVDVideoCodec::VCReturn GetPicture(VideoPicture* pVideoPicture) override;
  const char* GetName() override { return m_name.c_str(); }
  unsigned GetAllowedReferences() override { return MMAL_CODEC_NUM_BUFFERS; }
  void SetCodecControl(int flags) override;
  void SetSpeed(int iSpeed) override;
  bool GetCodecStats(double& pts, int& droppedFrames, int& skippedPics) override;

protected:
  void Process() override;

private:
  static void ProcessControlCallback(MMALPort port, MMALBufferHeader header);
  static void ProcessInputCallback(MMALPort port, MMALBufferHeader header);
  static void ProcessOutputCallback(MMALPort port, MMALBufferHeader header);
  static void BufferReleaseCallback(CVideoBufferPoolMMAL* pool,
                                    CVideoBufferMMAL* buffer,
                                    void* userdata);

  bool Close(bool force = false);
  bool ConfigureCodec(uint8_t* extraData, uint32_t extraDataSize);

  bool SendEndOfStream();
  void UpdateProcessInfo();

  std::atomic<MMALCodecState> m_state{MCS_UNINITIALIZED};

  std::string m_name;
  std::string m_codecName;
  MMALComponent m_component;

  MMALPort m_input{nullptr};
  MMALPool m_inputPool{nullptr};

  CCriticalSection m_portLock;

  MMALPort m_output{nullptr};
  MMALFormat m_portFormat{nullptr};
  
  CCriticalSection m_sendLock;
  CCriticalSection m_recvLock;

  int m_playbackSpeed{DVD_PLAYSPEED_NORMAL};
  int m_codecControlFlags{0};
  AVPixelFormat m_format{AV_PIX_FMT_NONE};

  int64_t m_ptsCurrent{MMAL_TIME_UNKNOWN};
  int32_t m_droppedFrames{-1};

  uint32_t m_rejectedSize{0};

  uint32_t m_width{0};
  uint32_t m_height{0};
  uint32_t m_displayWidth{0};
  uint32_t m_displayHeight{0};

  uint32_t m_supportedCodecs[16]{0};

  float m_fps{0.0f};
  float m_aspect{0.0f};

  uint32_t m_fpsRate{0};
  uint32_t m_fpsScale{0};

  bool m_dropped{false};
  std::deque<CVideoBufferMMAL*> m_buffers;

  XbmcThreads::ConditionVariable m_bufferCondition;

  CDVDStreamInfo m_hints;
  std::shared_ptr<IVideoBufferPool> m_bufferPool;
};
} // namespace MMAL