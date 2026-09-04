// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <capnp/ez-rpc.h>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <xkbcommon/xkbcommon.h>

struct XkbConfig {
  std::string rules;
  std::string model;
  std::string layout;
  std::string variant;
  std::string options;
};

#include "SyscallInterface.h"

#if ENABLE_RECORDER
#include "TestRunnerRecorder.h"
#endif
#include "spdlog/spdlog.h"

class ErrorHandlerImpl : public kj::TaskSet::ErrorHandler {
 public:
  void taskFailed(kj::Exception&& exception) override {
    spdlog::error("KJ Async Error: {}", exception.getDescription().cStr());
  }
};
#include "plugins.h"

#include "capnp/test_runner.capnp.h"
#include "capnp/test_runner_server.capnp.h"

enum {
  VIRTUAL_MOUSE = 0,
  VIRTUAL_KEYBOARD,
  VIRTUAL_TOUCHSCREEN,
  VIRTUAL_MULTI_TOUCHSCREEN,
  MAX_DEVICES
};

#define DEFAULT_PORT_NUMBER 4004

#define MAX_CUSTOM_RECORDERS 20

class TestRunnerServer final : virtual public TestRunnerService::Server,
                               public TestRunnerPlugins {
 public:
  explicit TestRunnerServer(SyscallInterface* syscalls = nullptr,
                            bool enable_plugins = true,
                            bool enable_snapshot_recorder = true,
                            const XkbConfig& xkb_cfg = {});
  ~TestRunnerServer();

  void emit(int fd, int type, int code, int val);
  int getDeviceFd(uint32_t device) {
    if (device < MAX_DEVICES) {
      return mDeviceFd[device];
    }
    return -1;
  }

  // Input interface
  kj::Promise<void> handleMouseMove(HandleMouseMoveContext context) override;
  kj::Promise<void> handleMouseClick(HandleMouseClickContext context) override;
  kj::Promise<void> handleKeyPress(HandleKeyPressContext context) override;
  kj::Promise<void> handleSingleTouch(
      HandleSingleTouchContext context) override;
  kj::Promise<void> handleMultiTouch(HandleMultiTouchContext context) override;
  kj::Promise<void> handlePassThrough(
      HandlePassThroughContext context) override;

  // Recorder interface. Not overridden without BUILD_RECORDER; capnp's
  // generated bodies answer "unimplemented".
#if ENABLE_RECORDER
  kj::Promise<void> getSnapshot(GetSnapshotContext context) override;
  kj::Promise<void> createCustomRecorder(
      CreateCustomRecorderContext context) override;
  kj::Promise<void> listDevices(ListDevicesContext context) override;
  kj::Promise<void> setDevicesToRecord(
      SetDevicesToRecordContext context) override;
  kj::Promise<void> startCustomRecording(
      StartCustomRecordingContext context) override;
  kj::Promise<void> stopCustomRecording(
      StopCustomRecordingContext context) override;
  kj::Promise<void> checkRecorderActive(
      CheckRecorderActiveContext context) override;
#endif

 private:
  int mDeviceFd[MAX_DEVICES];
#if ENABLE_RECORDER
  std::unique_ptr<TestRunnerRecorder> snapshot_recorder;
  std::vector<std::unique_ptr<TestRunnerRecorder>> custom_recorders;
#endif
  std::unique_ptr<RealSyscalls> m_owned_syscalls;
  SyscallInterface* m_syscalls;

  xkb_context* m_xkb_ctx = nullptr;
  xkb_keymap* m_xkb_keymap = nullptr;

  void setup_virtual_mouse();
  void setup_virtual_keyboard();
  void setup_virtual_touchscreen();
  void setup_virtual_multi_touchscreen();
  void setup_xkb(const XkbConfig& cfg);
  uint32_t resolve_keysym(xkb_keysym_t sym) const;
  void uinput_capability_setup(int fd, int type, int val);
  void close_input_device(int fd);
};
