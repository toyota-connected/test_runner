// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include "WaylandScreenshooter.h"
#include "TestRunnerServer.h"
#include "WaylandCapture.h"

kj::Promise<void> PluginWaylandScreenshooter::takeScreenshot(
    TakeScreenshotContext context) {
  std::string outputName = context.getParams().getOutputName();
  ScreenshotResult result = wayland_capture_screenshot(outputName);
  if (!result.error.empty()) {
    return kj::Exception(kj::Exception::Type::FAILED, __FILE__, __LINE__,
                         kj::str(result.error));
  }
  auto ret = context.getResults();
  ret.setImageData(
      kj::arrayPtr(result.imageData.data(), result.imageData.size()));
  ret.setWidth(result.width);
  ret.setHeight(result.height);
  return kj::READY_NOW;
}
