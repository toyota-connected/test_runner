// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include "RPCClient.h"

#ifdef ENABLE_PLUGIN_WESTON_SCREENSHOOTER
#include "weston_screenshooter/WestonScreenshooter_Client.h"
#endif
#ifdef ENABLE_PLUGIN_AGL_HEALTH
#include "agl_health/AglHealth_Client.h"
#endif
#ifdef ENABLE_PLUGIN_AGL_SCREENSHOOTER
#include "agl_screenshooter/AglScreenshooter_Client.h"
#endif

class TestRunnerPluginsClient
    : virtual public RPCClient
#ifdef ENABLE_PLUGIN_WESTON_SCREENSHOOTER
    , public PluginWestonScreenshooterClient
#endif
#ifdef ENABLE_PLUGIN_AGL_HEALTH
    , public PluginAglHealthClient
#endif
#ifdef ENABLE_PLUGIN_AGL_SCREENSHOOTER
    , public PluginAglScreenshooterClient
#endif
{
 public:
  TestRunnerPluginsClient() = default;
  ~TestRunnerPluginsClient() = default;
};
