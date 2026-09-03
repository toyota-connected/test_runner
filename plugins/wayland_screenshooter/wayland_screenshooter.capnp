# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
@0x93c6e562d9faf9bd;

# Plugin: WaylandScreenshooter
# Captures a screenshot via the Wayland screenshooter protocol. At runtime the
# plugin auto-detects whether the compositor advertises agl_screenshooter or
# weston_screenshooter and uses whichever is available.
interface Screenshooter {
  takeScreenshot @0 (outputName :Text) -> (imageData :Data, width :UInt32, height :UInt32);
}
