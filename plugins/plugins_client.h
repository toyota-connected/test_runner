// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include "weston_screenshooter/WestonScreenshooter_Client.h"

class TestRunnerPluginsClient : public PluginWestonScreenshooterClient {
 public:
  TestRunnerPluginsClient() = default;
  ~TestRunnerPluginsClient() = default;
};