/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RPProcessInfoDmx.h"

using namespace KODI;
using namespace RETRO;

CRPProcessInfoDmx::CRPProcessInfoDmx() : CRPProcessInfo("DMX")
{
}

CRPProcessInfo* CRPProcessInfoDmx::Create()
{
  return new CRPProcessInfoDmx();
}

void CRPProcessInfoDmx::Register()
{
  CRPProcessInfo::RegisterProcessControl(CRPProcessInfoDmx::Create);
}
