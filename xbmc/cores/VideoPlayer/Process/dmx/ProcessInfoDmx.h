/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/IPlayer.h"
#include "cores/VideoPlayer/Process/ProcessInfo.h"

namespace VIDEOPLAYER
{
class CProcessInfoDmx : public CProcessInfo
{
public:
  static CProcessInfo* Create();
  static void Register();

  CProcessInfoDmx();
  EINTERLACEMETHOD GetFallbackDeintMethod() override;
};
} // namespace VIDEOPLAYER
