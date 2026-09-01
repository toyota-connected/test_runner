# test_runner

[![CI](https://github.com/<org>/test_runner/actions/workflows/unit_tests.yml/badge.svg)](https://github.com/<org>/test_runner/actions/workflows/unit_tests.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

Remote test framework for automated testing of [ivi-homescreen](https://github.com/toyota-connected/ivi-homescreen). Cap'n Proto server daemon runs alongside ivi-homescreen and listens for remote events.  Enables injecting and recording input events via `uinput`. A plugin system lets developers extend the server with their own Cap'n Proto interfaces.

## Features

- **Remote Input** — mouse, keyboard, touchscreen, multi-touch, and raw passthrough over the network
- **Recording** — capture `/dev/input` events to file for later replay
- **Screenshot** — capture Weston compositor output (via the WestonScreenshooter plugin)
- **Plugin system** — add new Cap'n Proto interfaces without touching core server code

> **Security notice:** test_runner binds to all network interfaces on port 4004 with no authentication or encryption. It is designed exclusively for isolated test lab networks. Never expose this service on an untrusted network.

## Architecture

```
Test Harness / CI                  Device Under Test
─────────────────                  ─────────────────
Python/C++ client ──TCP:4004──► test-runner-server
                                       │
                                 uinput devices
                                  (mouse, kbd,
                                  touchscreen)
```

## Prerequisites

```
sudo apt-get install capnproto libcapnp-dev libwayland-dev libinput-dev
pip install pycapnp pillow
```

## Building

```bash
mkdir build && cd build
cmake .. -DBUILD_SERVER=ON -DBUILD_EXAMPLES=ON
make -j$(nproc)
```

Client only (cross-platform, no Wayland/uinput required):

```bash
cmake .. -DBUILD_SERVER=OFF -DBUILD_EXAMPLES=OFF
```

The build bundles `capnproto 0.9.1` (via `third_party/`) by default. To use a system-installed capnproto instead:

```bash
cmake .. -DBUILD_CAPNP=OFF
```

Minimum required system capnproto version: **0.9.x**.

Recording is on by default. For an injection-only server, with no
`libTestRunnerRecorder`, no `TestRunner-Recorder` and no snapshot recorder at
startup:

```bash
cmake .. -DBUILD_SERVER=ON -DBUILD_EXAMPLES=ON -DBUILD_RECORDER=OFF
```

The `Recorder` methods stay in the schema and answer `unimplemented`, so clients
need no rebuild.

## Permissions

Recording inputs requires the user to be part of group 'input'.  

Creating virtual devices requires the user to be part of group 'uinput'.

If active user is not part of both groups, it is recommended that the server is run with 'sudo'.

## Running

Terminal 1 (add sudo if necessary):

```bash
TestRunner-Server
```

Terminal 2:

```bash
# Screenshot (requires PIL)
scripts/getScreenshot.py --host localhost:4004 --output screen.png

# Send touch event
scripts/send_touch.py --host localhost:4004
```

## Plugin System

Plugins extend the server with new Cap'n Proto interfaces. WestonScreenshooter is the reference implementation. See `plugins/README.md` for instructions on creating plugins.

## Protocol

Default port: **4004**

Core interfaces are defined in `capnp/test_runner_v1.capnp` (Input, Recorder).  
The composed server interface is in `capnp/test_runner_server.capnp`.  
Plugin interfaces are in `plugins/plugins.capnp`.

## License

test_runner is licensed under the GNU General Public License v3.0. Any software that links against `libtest_runner_client.so` or `libtest_runner_server.so` must be distributed under GPLv3-compatible terms. See [LICENSE](LICENSE) for the full text.
