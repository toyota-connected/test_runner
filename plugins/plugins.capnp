# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
@0x92289ba1D7af2b1a;

using import "../plugins/weston_screenshooter/weston_screenshooter.capnp".Screenshooter;
using import "../plugins/agl_health/agl_health.capnp".AglHealth;
using import "../plugins/agl_screenshooter/agl_screenshooter.capnp".AglScreenshooter;

interface Plugins extends (Screenshooter, AglHealth, AglScreenshooter) {
}