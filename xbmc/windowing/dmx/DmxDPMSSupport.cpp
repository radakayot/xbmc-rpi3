/*
 *  Copyright (C) 2009-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DmxDPMSSupport.h"

#include "ServiceBroker.h"
#include "windowing/dmx/WinSystemDmx.h"

using namespace KODI::WINDOWING::DMX;

CDmxDPMSSupport::CDmxDPMSSupport()
{
  m_supportedModes.push_back(OFF);
}

bool CDmxDPMSSupport::EnablePowerSaving(PowerSavingMode mode)
{
  auto winSystem = dynamic_cast<CWinSystemDmx*>(CServiceBroker::GetWinSystem());
  if (!winSystem)
    return false;

  switch (mode)
  {
    case OFF:
      return winSystem->Hide();
    default:
      return false;
  }
}

bool CDmxDPMSSupport::DisablePowerSaving()
{
  auto winSystem = dynamic_cast<CWinSystemDmx*>(CServiceBroker::GetWinSystem());
  if (!winSystem)
    return false;

  return winSystem->Show();
}
