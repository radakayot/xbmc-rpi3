/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ProcessInfoDmx.h"

#include "cores/VideoPlayer/Buffers/VideoBufferPoolMMAL.h"

using namespace VIDEOPLAYER;

CProcessInfo* CProcessInfoDmx::Create()
{
  return new CProcessInfoDmx();
}

void CProcessInfoDmx::Register()
{
  CProcessInfo::RegisterProcessControl("dmx", CProcessInfoDmx::Create);
}

CProcessInfoDmx::CProcessInfoDmx()
{
  m_videoBufferManager.RegisterPool(std::make_shared<MMAL::CVideoBufferPoolMMAL>());
}

EINTERLACEMETHOD CProcessInfoDmx::GetFallbackDeintMethod()
{
#if defined(__arm__)
  return EINTERLACEMETHOD::VS_INTERLACEMETHOD_DEINTERLACE_HALF;
#else
  return CProcessInfo::GetFallbackDeintMethod();
#endif
}
