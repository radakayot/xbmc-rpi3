/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "VideoSyncDmx.h"

#include "ServiceBroker.h"
#include "threads/Thread.h"
#include "utils/TimeUtils.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"
#include "windowing/WinSystem.h"

bool CVideoSyncDmx::Setup(PUPDATECLOCK func)
{
  m_winSystem = dynamic_cast<KODI::WINDOWING::DMX::CWinSystemDmx*>(CServiceBroker::GetWinSystem());

  if (!m_winSystem)
  {
    CLog::Log(LOGWARNING, "CVideoSyncDmx::{}: failed to get winSystem", __FUNCTION__);
    return false;
  }

  UpdateClock = func;
  m_abort = false;
  m_winSystem->Register(this);
  return true;
}

bool CVideoSyncDmx::AdjustThreadPriority(pthread_t t)
{
  struct sched_param sp;
  int p = SCHED_FIFO;
  if (pthread_getschedparam(t, &p, &sp) == 0)
  {
    p = SCHED_FIFO;
    sp.sched_priority = sched_get_priority_max(p);
    return pthread_setschedparam(t, p, &sp) == 0;
  }
  return false;
}

void CVideoSyncDmx::Run(CEvent& stopEvent)
{
  uint64_t sequence = 0, last_sequence = 0, time = 0, skew = 0;
  AdjustThreadPriority(pthread_self());

  last_sequence = m_winSystem->WaitVerticalSync(m_winSystem->WaitVerticalSync(0) + 1, &time);
  skew = CurrentHostCounter() - time;
  while (!stopEvent.Signaled() && !m_abort)
  {
    sequence = m_winSystem->WaitVerticalSync(last_sequence + 1, &time);
    UpdateClock(sequence - last_sequence, time + skew, m_refClock);
    last_sequence = sequence;
  }
}

void CVideoSyncDmx::Cleanup()
{
  m_winSystem->Unregister(this);
}

float CVideoSyncDmx::GetFps()
{
  m_fps = m_winSystem->GetGfxContext().GetFPS();
  return m_fps;
}

void CVideoSyncDmx::OnResetDisplay()
{
  m_abort = true;
}

void CVideoSyncDmx::RefreshChanged()
{
  if (m_fps != m_winSystem->GetGfxContext().GetFPS())
    m_abort = true;
}
