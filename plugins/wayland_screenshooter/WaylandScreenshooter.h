// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include "capnp/test_runner_server.capnp.h"

class PluginWaylandScreenshooter : virtual public TestRunnerService::Server {
 public:
  PluginWaylandScreenshooter() = default;
  ~PluginWaylandScreenshooter() = default;

  kj::Promise<void> takeScreenshot(TakeScreenshotContext context) override;
};
