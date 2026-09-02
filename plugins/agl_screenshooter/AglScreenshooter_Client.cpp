// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include "AglScreenshooter_Client.h"
#include <kj/debug.h>
#include <cstring>
#include "spdlog/spdlog.h"

AglScreenshotResult PluginAglScreenshooterClient::AglTakeScreenshot() {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto screenshooter = getMain<AglScreenshooter>();
  auto response =
      waitWithTimeout(screenshooter.aglTakeScreenshotRequest().send());
  spdlog::debug("AglTakeScreenshot response: {}x{}", response.getWidth(),
                response.getHeight());
  AglScreenshotResult result;
  auto data = response.getImageData();
  result.imageData.assign(data.begin(), data.end());
  result.width = response.getWidth();
  result.height = response.getHeight();
  return result;
}
