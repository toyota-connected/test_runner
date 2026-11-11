// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include "WaylandScreenshooter_Client.h"
#include <kj/debug.h>
#include "spdlog/spdlog.h"

ScreenshotResult PluginWaylandScreenshooterClient::TakeScreenshot(
    const std::string& outputName) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto screenshooter = getMain<Screenshooter>();
  auto request = screenshooter.takeScreenshotRequest();
  if (!outputName.empty())
    request.setOutputName(outputName);
  auto response = waitWithTimeout(request.send());
  spdlog::debug("TakeScreenshot response: {}x{}", response.getWidth(),
                response.getHeight());
  ScreenshotResult result;
  auto data = response.getImageData();
  result.imageData.assign(data.begin(), data.end());
  result.width = response.getWidth();
  result.height = response.getHeight();
  return result;
}
