// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "capnp/test_runner_server.capnp.h"

struct AglScreenshotResult {
  std::vector<uint8_t> imageData;  // raw BGRA pixels
  uint32_t width{0};
  uint32_t height{0};
  std::string error;
};

class PluginAglScreenshooter : virtual public TestRunnerService::Server {
 public:
  PluginAglScreenshooter() = default;
  ~PluginAglScreenshooter() = default;

  kj::Promise<void> aglTakeScreenshot(AglTakeScreenshotContext context) override;

 private:
  static AglScreenshotResult capture();
};
