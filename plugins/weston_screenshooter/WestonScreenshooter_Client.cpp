// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include "WestonScreenshooter_Client.h"
#include <kj/debug.h>
#include <cstring>
#include "spdlog/spdlog.h"

ScreenshotResult PluginWestonScreenshooterClient::TakeScreenshot() {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto screenshooter = getMain<Screenshooter>();
  auto response = waitWithTimeout(screenshooter.takeScreenshotRequest().send());
  spdlog::debug("TakeScreenshot response: {}x{}", response.getWidth(),
                response.getHeight());
  ScreenshotResult result;
  auto data = response.getImageData();
  result.imageData.assign(data.begin(), data.end());
  result.width = response.getWidth();
  result.height = response.getHeight();
  return result;
}
