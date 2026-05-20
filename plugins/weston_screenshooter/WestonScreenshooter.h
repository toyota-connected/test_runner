// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "capnp/test_runner_server.capnp.h"

struct ScreenshotResult {
  std::vector<uint8_t> imageData;  // raw BGRA pixels
  uint32_t width{0};
  uint32_t height{0};
  std::string error;
};

class PluginWestonScreenshooter : virtual public TestRunnerService::Server {
 public:
  PluginWestonScreenshooter() = default;
  ~PluginWestonScreenshooter() = default;

  kj::Promise<void> takeScreenshot(TakeScreenshotContext context) override;

 private:
  static ScreenshotResult capture();
};