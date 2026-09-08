// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <wayland-client.h>

#include "ScreenshotResult.h"

struct WaylandOutputInfo {
  wl_output* output{nullptr};
  std::string
      name;  // connector name from wl_output.name event (v4), e.g. "HDMI-A-1"
  std::string model;  // EDID model string from wl_output.geometry
  int32_t width{0};
  int32_t height{0};
};

struct WaylandCaptureBase {
  wl_display* display{nullptr};
  wl_registry* registry{nullptr};
  wl_shm* shm{nullptr};
  std::vector<WaylandOutputInfo> outputs;
  bool finished{false};
  ScreenshotResult result;
};

struct WaylandShmBuffer {
  wl_buffer* buffer{nullptr};
  void* data{nullptr};
  size_t size{0};
};

WaylandOutputInfo* find_output(WaylandCaptureBase* s, wl_output* output);

// Select output by connector name (wl_output.name, e.g. "HDMI-A-1").
// Returns nullptr if preferred is non-empty and no output matches, or if there
// are no outputs at all.  Pass empty string to pick the first available output.
WaylandOutputInfo* select_output(WaylandCaptureBase* s,
                                 const std::string& preferred = "");

WaylandShmBuffer create_shm_buffer(WaylandCaptureBase* s,
                                   int32_t width,
                                   int32_t height);

void destroy_shm_buffer(WaylandShmBuffer& buf);

extern const wl_output_listener wayland_output_listener;

bool bind_common_globals(WaylandCaptureBase* s,
                         wl_registry* registry,
                         uint32_t name,
                         const char* interface,
                         uint32_t version);

void cleanup_wayland_base(WaylandCaptureBase* s);

// Auto-detecting screenshot capture. Connects to the Wayland display, discovers
// which screenshooter protocol the compositor advertises (agl_screenshooter or
// weston_screenshooter), and captures a frame using it.
ScreenshotResult wayland_capture_screenshot(
    const std::string& output_name = "");
