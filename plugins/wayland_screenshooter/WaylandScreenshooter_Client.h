// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <string>
#include "RPCClient.h"
#include "capnp/test_runner_server.capnp.h"
#include "ScreenshotResult.h"

class PluginWaylandScreenshooterClient : virtual public RPCClient {
 public:
  PluginWaylandScreenshooterClient() = default;
  ~PluginWaylandScreenshooterClient() = default;

  ScreenshotResult TakeScreenshot(const std::string& outputName = "");
};
