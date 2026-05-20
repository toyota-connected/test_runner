# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
@0xa1b2c3d4e5f60001;

# Plugin: WestonScreenshooter
# Captures a screenshot from the Weston compositor via the weston-screenshooter
# Wayland protocol. The server process must have access to WAYLAND_DISPLAY.
interface Screenshooter {
  takeScreenshot @0 () -> (imageData :Data, width :UInt32, height :UInt32);
}
