/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/RetroPlayer/process/RPProcessInfo.h"

namespace KODI
{
namespace RETRO
{
class CRPProcessInfoDmx : public CRPProcessInfo
{
public:
  CRPProcessInfoDmx();

  static CRPProcessInfo* Create();
  static void Register();
};
} // namespace RETRO
} // namespace KODI
