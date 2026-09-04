// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include "agl_health/AglHealth.h"
#include "weston_screenshooter/WestonScreenshooter.h"

class TestRunnerPlugins : public PluginWestonScreenshooter,
                          public PluginAglHealth {
 public:
  TestRunnerPlugins() = default;
  ~TestRunnerPlugins() = default;

  void init_plugins();
};
