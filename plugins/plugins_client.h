// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include "RPCClient.h"

#ifdef ENABLE_PLUGIN_WAYLAND_SCREENSHOOTER
#include "wayland_screenshooter/WaylandScreenshooter_Client.h"
#endif
#ifdef ENABLE_PLUGIN_AGL_HEALTH
#include "agl_health/AglHealth_Client.h"
#endif

// clang-format off
// The base-class list is preprocessor-guarded; clang-format moves the
// leading commas onto their own lines when it reformats across #ifdef.
class TestRunnerPluginsClient
    : virtual public RPCClient
#ifdef ENABLE_PLUGIN_WAYLAND_SCREENSHOOTER
    , public PluginWaylandScreenshooterClient
#endif
#ifdef ENABLE_PLUGIN_AGL_HEALTH
    , public PluginAglHealthClient
#endif
{
  // clang-format on
 public:
  TestRunnerPluginsClient() = default;
  ~TestRunnerPluginsClient() = default;
};
