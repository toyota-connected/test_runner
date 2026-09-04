// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ScreenshotResult {
  std::vector<uint8_t> imageData;  // raw XRGB8888 pixels
  uint32_t width{0};
  uint32_t height{0};
  std::string error;
};
