// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#ifdef ENABLE_PLUGIN_WESTON_SCREENSHOOTER
#include "weston_screenshooter/WestonScreenshooter_Client.h"
#endif
#ifdef ENABLE_PLUGIN_AGL_HEALTH
#include "agl_health/AglHealth_Client.h"
#endif

class TestRunnerPluginsClient
#if defined(ENABLE_PLUGIN_WESTON_SCREENSHOOTER)
    : public PluginWestonScreenshooterClient
#if defined(ENABLE_PLUGIN_AGL_HEALTH)
    , public PluginAglHealthClient
#endif
#elif defined(ENABLE_PLUGIN_AGL_HEALTH)
    : public PluginAglHealthClient
#endif
{
 public:
  TestRunnerPluginsClient() = default;
  ~TestRunnerPluginsClient() = default;
};