/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/VideoPlayer/Buffers/VideoBuffer.h"
#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodec.h"
#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodecMMAL.h"
#include "cores/VideoPlayer/DVDStreamInfo.h"
#include "threads/Thread.h"

#include <memory>

#define MMAL_FFMPEG_CODEC_NUM_BUFFERS (MMAL_CODEC_NUM_BUFFERS * 3 / 2)

namespace MMAL
{
class CDVDVideoCodecFFmpegMMAL : public CDVDVideoCodec, public CThread
{
public:
  explicit CDVDVideoCodecFFmpegMMAL(CProcessInfo& processInfo);
  ~CDVDVideoCodecFFmpegMMAL() override;

  static std::unique_ptr<CDVDVideoCodec> CreateCodec(CProcessInfo& processInfo);
  static void Register();

  bool Open(CDVDStreamInfo& hints, CDVDCodecOptions& options) override;
  bool AddData(const DemuxPacket& packet) override;
  void Reset() override;
  CDVDVideoCodec::VCReturn GetPicture(VideoPicture* pVideoPicture) override;
  const char* GetName() override { return m_name.c_str(); }
  unsigned GetAllowedReferences() override { return MMAL_CODEC_NUM_BUFFERS; }
  void SetCodecControl(int flags) override;
  bool GetCodecStats(double& pts, int& droppedFrames, int& skippedPics) override;

protected:
  void Process() override;

private:
  static enum AVPixelFormat GetFormatCallback(struct AVCodecContext* avctx,
                                              const enum AVPixelFormat* fmt);
  void UpdateProcessInfo();
  const char* GetStatusString(int status);
  void Flush();

  std::atomic<MMALCodecState> m_state{MCS_UNINITIALIZED};

  std::string m_name;

  AVCodecContext* m_context{nullptr};
  AVCodec* m_codec{nullptr};
  AVFrame* m_frame{nullptr};
  MMALFormat m_portFormat{nullptr};

  CCriticalSection m_bufferLock;
  CCriticalSection m_queueLock;

  int m_playbackSpeed{DVD_PLAYSPEED_NORMAL};
  int m_codecControlFlags{0};
  AVPixelFormat m_format{AV_PIX_FMT_NONE};

  int64_t m_ptsCurrent{AV_NOPTS_VALUE};
  int32_t m_droppedFrames{-1};

  uint32_t m_width{0};
  uint32_t m_height{0};
  uint32_t m_displayWidth{0};
  uint32_t m_displayHeight{0};

  float m_fps{0.0f};
  float m_aspect{0.0f};

  std::atomic<bool> m_receive{false};
  bool m_dropped{false};
  CDVDStreamInfo m_hints;
  std::shared_ptr<CVideoBufferPoolMMAL> m_bufferPool;
};
} // namespace MMAL