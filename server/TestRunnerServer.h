// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <capnp/ez-rpc.h>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <cstdint>
#include <memory>
#include <vector>

#include "SyscallInterface.h"

#include "TestRunnerRecorder.h"
#include "spdlog/spdlog.h"

class ErrorHandlerImpl : public kj::TaskSet::ErrorHandler {
 public:
  void taskFailed(kj::Exception&& exception) override {
    spdlog::error("KJ Async Error: {}", exception.getDescription().cStr());
  }
};
#include "plugins.h"

#include "capnp/test_runner_server.capnp.h"
#include "capnp/test_runner.capnp.h"

enum {
  VIRTUAL_MOUSE = 0,
  VIRTUAL_KEYBOARD,
  VIRTUAL_TOUCHSCREEN,
  VIRTUAL_MULTI_TOUCHSCREEN,
  MAX_DEVICES
};

#define DEFAULT_PORT_NUMBER 4004

#define MAX_CUSTOM_RECORDERS 20

class TestRunnerServer final : virtual public TestRunnerService::Server, public TestRunnerPlugins {
 public:
  explicit TestRunnerServer(SyscallInterface* syscalls = nullptr,
                     bool enable_plugins = true,
                     bool enable_snapshot_recorder = true);
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

  // Recorder interface
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

 private:
  int mDeviceFd[MAX_DEVICES];
  std::unique_ptr<TestRunnerRecorder> snapshot_recorder;
  std::vector<std::unique_ptr<TestRunnerRecorder>> custom_recorders;
  std::unique_ptr<RealSyscalls> m_owned_syscalls;
  SyscallInterface* m_syscalls;

  void setup_virtual_mouse();
  void setup_virtual_keyboard();
  void setup_virtual_touchscreen();
  void setup_virtual_multi_touchscreen();
  void uinput_capability_setup(int fd, int type, int val);
  void close_input_device(int fd);
};