# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
@0xa3b4c5d6e7f80001;

# Plugin: AglScreenshooter
# Captures a screenshot from the AGL compositor via the agl-screenshooter
# Wayland protocol. The server process must have access to WAYLAND_DISPLAY.
interface AglScreenshooter {
  aglTakeScreenshot @0 () -> (imageData :Data, width :UInt32, height :UInt32);
}
