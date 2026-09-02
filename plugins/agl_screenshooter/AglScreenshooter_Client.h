// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <string>
#include <vector>
#include "RPCClient.h"
#include "capnp/test_runner_server.capnp.h"

struct AglScreenshotResult {
  std::vector<uint8_t> imageData;  // raw BGRA pixels
  uint32_t width{0};
  uint32_t height{0};
  std::string error;
};

class PluginAglScreenshooterClient : virtual public RPCClient {
 public:
  PluginAglScreenshooterClient() = default;
  ~PluginAglScreenshooterClient() = default;

  AglScreenshotResult AglTakeScreenshot();
};
