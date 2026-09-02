// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#ifdef ENABLE_PLUGIN_WESTON_SCREENSHOOTER
#include "weston_screenshooter/WestonScreenshooter.h"
#endif
#ifdef ENABLE_PLUGIN_AGL_HEALTH
#include "agl_health/AglHealth.h"
#endif

class TestRunnerPlugins
#if defined(ENABLE_PLUGIN_WESTON_SCREENSHOOTER)
    : public PluginWestonScreenshooter
#if defined(ENABLE_PLUGIN_AGL_HEALTH)
    , public PluginAglHealth
#endif
#elif defined(ENABLE_PLUGIN_AGL_HEALTH)
    : public PluginAglHealth
#endif
{
 public:
  TestRunnerPlugins() = default;
  ~TestRunnerPlugins() = default;

  void init_plugins();
};
