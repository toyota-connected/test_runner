# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **`DISABLE_PLUGINS` CMake option** — pass `-DDISABLE_PLUGINS=ON` to turn off all plugins at once
- **CI plugin matrix** — Ubuntu 22.04 workflow now builds all-plugins, no-plugins, and custom-plugins configurations

### Fixed

- **Plugin toggling** — `ENABLE_PLUGIN_*` CMake options now correctly exclude disabled plugins from the build via `#ifdef` guards in `plugins.h` and `plugins_client.h`

### Changed

- **Plugin list consolidation** — `TEST_RUNNER_PLUGINS` in `cmake/plugins.cmake` is now the single source of truth for plugin registration; `add_subdirectory()` and capnp schema collection are driven from it

## [0.1.0] — 2026-05-07

Initial public release.

### Added

- **Input injection** — remote mouse, keyboard, single-touch, multi-touch, and raw `EV_*` passthrough via `uinput`
- **Recorder** — capture `/dev/input` events to `.rrc` files; supports fixed-length and continuous (rolling-window) modes; up to 20 concurrent recorder instances
- **Plugin system** — extend the server with new Cap'n Proto interfaces without modifying core code; `WestonScreenshooter` is the reference plugin
- **WestonScreenshooter plugin** — capture the Weston compositor framebuffer over the Wayland `screenshooter` protocol
- **C++ client library** (`libTestRunnerClient.so`) — typed wrappers for all RPC calls with a 10-second per-call timeout
- **Python scripts** — `getScreenshot.py`, `getSnapshot.py`, `send_touch.py`
- **Standalone recorder** (`TestRunner-Recorder`) — record input events without running the full server
- **Cap'n Proto schemas** — `capnp/test_runner_v1.capnp` (Input, Recorder), `capnp/test_runner_server.capnp` (composed service), `plugins/plugins.capnp`
- **CMake build** with optional bundled capnproto 0.9.1 (`-DBUILD_CAPNP=ON`) and optional server build (`-DBUILD_SERVER=ON`)
- **Unit test suite** with mock syscall layer for server and recorder

[0.1.0]: https://github.com/<org>/test_runner/releases/tag/v0.1.0
