// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include "capnp/test_runner_server.capnp.h"

#ifdef ENABLE_PLUGIN_WAYLAND_SCREENSHOOTER
#include "wayland_screenshooter/WaylandScreenshooter.h"
#endif
#ifdef ENABLE_PLUGIN_AGL_HEALTH
#include "agl_health/AglHealth.h"
#endif

// clang-format off
// The base-class list is preprocessor-guarded; clang-format moves the
// leading commas onto their own lines when it reformats across #ifdef.
class TestRunnerPlugins
    : virtual public TestRunnerService::Server
#ifdef ENABLE_PLUGIN_WAYLAND_SCREENSHOOTER
    , public PluginWaylandScreenshooter
#endif
#ifdef ENABLE_PLUGIN_AGL_HEALTH
    , public PluginAglHealth
#endif
{
  // clang-format on
 public:
  TestRunnerPlugins() = default;
  ~TestRunnerPlugins() = default;

  void init_plugins();
};
