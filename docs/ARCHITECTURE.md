# Architecture

test_runner is a Cap'n Proto RPC service that creates virtual input devices via the Linux `uinput` subsystem and exposes them over a TCP socket. A test harness on a separate machine (or the same machine) connects to the socket and sends structured RPC calls to inject input events or capture screen output.

## Component Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        Client side                              │
│                                                                 │
│  TestRunnerClient (C++)       scripts/ (Python)                 │
│  libTestRunnerClient.so       getScreenshot.py                  │
│         │                     send_touch.py                     │
│         └──────────────────────────┐                            │
└────────────────────────────────────│────────────────────────────┘
                                     │  TCP :4004
                                     │  Cap'n Proto two-party RPC
┌────────────────────────────────────│────────────────────────────┐
│                        Server side (DUT)                        │
│                                    ▼                            │
│              ┌──────────────────────────────────┐               │
│              │        TestRunnerServer           │               │
│              │   (TestRunnerService::Server)     │               │
│              │                                  │               │
│              │  Input       Recorder   Plugins  │               │
│              │  interface   interface  interface│               │
│              └────┬──────────────┬──────────────┘               │
│                   │              │                               │
│          ┌────────▼───┐  ┌───────▼──────────────┐               │
│          │  /dev/uinput│  │ TestRunnerRecorder   │               │
│          │  (4 virtual │  │ /dev/input/* polling │               │
│          │   devices)  │  │ .rrc files in /tmp/  │               │
│          └────────────┘  └──────────────────────┘               │
│                                                                  │
│  WestonScreenshooter plugin ──► Wayland compositor               │
└─────────────────────────────────────────────────────────────────┘
```

## Shared Libraries and Executables

| Target | Type | Purpose |
|--------|------|---------|
| `libTestRunnerClient.so` | shared lib | C++ client API; links into test harnesses |
| `libTestRunnerServer.so` | shared lib | Server implementation; used by the server executable and unit tests |
| `libTestRunnerRecorder.so` | shared lib | `/dev/input` recorder; used by both the server and the standalone recorder |
| `TestRunner-Server` | executable | Production server process (requires root) |
| `TestRunner-Recorder` | executable | Standalone recorder, usable without the server |
| `TestRunnerClientTest` | executable | Manual smoke-test client |

## Cap'n Proto Schema Layers

```
capnp/test_runner.capnp          — core Input and Recorder interfaces
capnp/test_runner_server.capnp   — TestRunnerService extends (Input, Recorder, Plugins)
plugins/plugins.capnp            — Plugins extends (Screenshooter)
plugins/weston_screenshooter/
  weston_screenshooter.capnp     — Screenshooter interface
```

`TestRunnerService` is the single bootstrap capability the server exports. Clients obtain it from `capnp::TwoPartyClient::bootstrap()` and cast it to `TestRunnerService`.

## Input Injection

`TestRunnerServer` opens four `uinput` file descriptors at construction time, one for each virtual device:

| Index | Device | uinput type |
|-------|--------|-------------|
| 0 | Mouse | `EV_REL` + buttons |
| 1 | Keyboard | `EV_KEY` |
| 2 | Single-touch screen | `EV_ABS` (`ABS_X`, `ABS_Y`) |
| 3 | Multi-touch screen | `EV_ABS` (`ABS_MT_*`) |

All four devices are created at startup regardless of which interfaces the client will use. Events are written with `write(fd, struct input_event, ...)` followed by `EV_SYN`.

**Multi-touch coordinate range:** The virtual multi-touch device is configured with `ABS_X` range `[0, VIRTUAL_MULTITOUCH_ABS_X_MAX]` (default 16000) and `ABS_Y` range `[0, VIRTUAL_MULTITOUCH_ABS_Y_MAX]` (default 9000). These are compile-time constants independent of physical display resolution. Override at build time:

```bash
cmake .. -DVIRTUAL_MULTITOUCH_ABS_X_MAX=4096 -DVIRTUAL_MULTITOUCH_ABS_Y_MAX=2160
```

## Recorder

`TestRunnerRecorder` enumerates `/dev/input/event*` nodes using `udev`, opens selected devices, and reads `struct input_event` from each in a select loop. Events are stored in an in-memory ring buffer (`std::queue`) and flushed to a `.rrc` file on stop.

Two recording modes:
- **Fixed-length** — records for a specified duration then stops.
- **Continuous** — keeps a rolling window of the last N seconds; writing the file captures the most recent window.

Up to `MAX_CUSTOM_RECORDERS` (20) recorder instances can run concurrently, each identified by an index returned from `createCustomRecorder`.

Filenames are sanitised with `std::filesystem::path::filename()` before concatenation into `/tmp/<name>.rrc` to prevent path traversal.

## Plugin System

Plugins add new Cap'n Proto interfaces to `TestRunnerService` without modifying core server code. Each plugin:

1. Defines a `.capnp` schema file exporting an interface.
2. Provides a C++ class that inherits `virtual public TestRunnerService::Server` and implements the interface methods.
3. Is listed in `plugins/plugins.capnp` (added to `extends`) and in `cmake/plugins.cmake`.

`TestRunnerServer` inherits from both `TestRunnerService::Server` (for core interfaces) and `TestRunnerPlugins` (which inherits all plugin classes). Cap'n Proto's virtual dispatch resolves the correct implementation per method.

The reference plugin is `WestonScreenshooter`, which uses the Weston `screenshooter` Wayland protocol extension to capture a raw BGRA framebuffer.

## RPC Transport

The server uses `capnp::EzRpcServer` bound to `*:4004` by default. The listen address can be overridden with the `-l` flag. The C++ client uses `capnp::TwoPartyClient` over a blocking TCP connection wrapped by `RPCClient`, which adds a 10-second per-call timeout. The Python clients use `pycapnp`'s async `TwoPartyClient` directly.

## Threading Model

Cap'n Proto's KJ event loop is single-threaded. All RPC handlers run on the same thread as the event loop. `TestRunnerRecorder` runs its own background thread per recorder instance for the select loop; writes back to the RPC thread happen via the in-memory queue with mutex protection.

## Security Model

test_runner provides **no authentication, no encryption, and no access control**. Any TCP client that can reach port 4004 can inject arbitrary input events. The service is intended exclusively for isolated test lab networks and must never be exposed on an untrusted network.

The server requires root (or `CAP_SYS_ADMIN` / `CAP_INPUT_ADMIN`) to open `/dev/uinput`.
