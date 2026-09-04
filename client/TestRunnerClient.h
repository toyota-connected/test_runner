// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "RPCClient.h"
#include "capnp/test_runner.capnp.h"
#include "capnp/test_runner_server.capnp.h"
#include "plugins_client.h"
#include "spdlog/spdlog.h"

class ErrorHandlerImpl : public kj::TaskSet::ErrorHandler {
 public:
  void taskFailed(kj::Exception&& exception) override {
    spdlog::error("KJ Async Error: {}", exception.getDescription().cStr());
  }
};

struct multiTouch {
  int slot;
  int tracking_id;
  int x;
  int y;
};

struct input_absinfo {
  int32_t value;
  int32_t minimum;
  int32_t maximum;
  int32_t fuzz;
  int32_t flat;
  int32_t resolution;
};

enum device_type {
  MOUSE = 0,
  KEYBOARD,
  TOUCHSCREEN,
  MULTITOUCHSCREEN,
  UNKNOWN
};

struct in_device_s {
  int fd;
  char name[50];
  device_type type;
  input_absinfo abs_range_x;
  input_absinfo abs_range_y;
};

class TestRunnerClient : virtual public RPCClient,
                         public TestRunnerPluginsClient {
 public:
  TestRunnerClient();
  ~TestRunnerClient() = default;

  // Remote Input
  void SendMouseMove(int x, int y);
  void SendMouseClick(int btn, bool pressed);
  void SendKeyPress(int key, bool pressed);
  void SendKeyPressByKeySym(uint32_t keySym, bool pressed);
  void SendKeyPressByName(const std::string& name, bool pressed);
  void SendSingleTouch(int x, int y, bool pressed);
  void SendMultiTouch(multiTouch touches[], int count);
  void SendPassThrough(int device, int type, int code, int val);

  // Recorder
  std::string getSnapshot();
  uint8_t createCustomRecorder(const std::string& filename,
                               uint32_t length,
                               bool continuous);
  void setDevicesToRecord(uint8_t index,
                          const std::vector<in_device_s>& devices);
  std::vector<in_device_s> listDevices(uint8_t index);
  void startCustomRecording(uint8_t index);
  std::string stopCustomRecording(uint8_t index);
  bool checkRecorderActive(uint8_t index);

  void WaitForTasks();

 private:
};