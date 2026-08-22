// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include "TestRunnerClient.h"

#include <cstring>

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/debug.h>

#include "spdlog/cfg/env.h"
#include "spdlog/spdlog.h"

static ErrorHandlerImpl errorHandler;
static kj::TaskSet tasks(errorHandler);

TestRunnerClient::TestRunnerClient() {
  spdlog::cfg::load_env_levels();
}

void TestRunnerClient::SendMouseMove(int x, int y) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto input = getMain<Input>();
  auto request = input.handleMouseMoveRequest();
  auto mouseMove = request.initMouseMove();
  mouseMove.setRelX(x);
  mouseMove.setRelY(y);
  waitWithTimeout(request.send());
  spdlog::debug("mouseMove sent: X={} Y={}", x, y);
}

void TestRunnerClient::SendMouseClick(int btn, bool pressed) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto input = getMain<Input>();
  auto request = input.handleMouseClickRequest();
  auto mouseClick = request.initMouseClick();
  mouseClick.setBtn(btn);
  mouseClick.setPressed(pressed);
  waitWithTimeout(request.send());
  spdlog::debug("mouseClick sent: btn={} pressed={}", btn, pressed);
}

void TestRunnerClient::SendKeyPress(int key, bool pressed) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto input = getMain<Input>();
  auto request = input.handleKeyPressRequest();
  auto keyPress = request.initKeyPress();
  keyPress.setRawCode(static_cast<uint16_t>(key));
  keyPress.setPressed(pressed);
  waitWithTimeout(request.send());
  spdlog::debug("keyPress sent: rawCode={} pressed={}", key, pressed);
}

void TestRunnerClient::SendKeyPressByKeySym(uint32_t keySym, bool pressed) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto input = getMain<Input>();
  auto request = input.handleKeyPressRequest();
  auto keyPress = request.initKeyPress();
  keyPress.setKeySym(keySym);
  keyPress.setPressed(pressed);
  waitWithTimeout(request.send());
  spdlog::debug("keyPress sent: keySym=0x{:x} pressed={}", keySym, pressed);
}

void TestRunnerClient::SendKeyPressByName(const std::string& name, bool pressed) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto input = getMain<Input>();
  auto request = input.handleKeyPressRequest();
  auto keyPress = request.initKeyPress();
  keyPress.setKeyName(name);
  keyPress.setPressed(pressed);
  waitWithTimeout(request.send());
  spdlog::debug("keyPress sent: keyName='{}' pressed={}", name, pressed);
}

void TestRunnerClient::SendSingleTouch(int x, int y, bool pressed) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto input = getMain<Input>();
  auto request = input.handleSingleTouchRequest();
  auto singleTouch = request.initSingleTouch();
  singleTouch.setAbsX(x);
  singleTouch.setAbsY(y);
  singleTouch.setPressed(pressed);
  waitWithTimeout(request.send());
  spdlog::debug("singleTouch sent: x={} y={} pressed={}", x, y, pressed);
}

void TestRunnerClient::SendMultiTouch(multiTouch touches[], int count) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto input = getMain<Input>();
  auto request = input.handleMultiTouchRequest();
  auto multiTouchList = request.initMultiTouch(count);
  for (int i = 0; i < count; i++) {
    multiTouchList[i].setSlot(static_cast<int8_t>(touches[i].slot));
    multiTouchList[i].setTrackingId(static_cast<int16_t>(touches[i].tracking_id));
    multiTouchList[i].setAbsX(touches[i].x);
    multiTouchList[i].setAbsY(touches[i].y);
  }
  waitWithTimeout(request.send());
  spdlog::debug("multiTouch sent: count={}", count);
}

void TestRunnerClient::SendPassThrough(int device, int type, int code, int val) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto input = getMain<Input>();
  auto request = input.handlePassThroughRequest();
  auto passThrough = request.initPassThrough();
  passThrough.setDevice(device);
  passThrough.setType(type);
  passThrough.setCode(code);
  passThrough.setValue(val);
  waitWithTimeout(request.send());
  spdlog::debug("passThrough sent: device={} type={} code={} value={}", device,
                type, code, val);
}

std::string TestRunnerClient::getSnapshot() {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto recorder = getMain<Recorder>();
  auto response = waitWithTimeout(recorder.getSnapshotRequest().send());
  return response.getPath();
}

uint8_t TestRunnerClient::createCustomRecorder(const std::string& filename,
                                        uint32_t length,
                                        bool continuous) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto recorder = getMain<Recorder>();
  auto request = recorder.createCustomRecorderRequest();
  request.setFilename(filename);
  request.setLength(length);
  request.setContinuous(continuous);
  auto response = waitWithTimeout(request.send());
  return response.getIndex();
}

void TestRunnerClient::setDevicesToRecord(uint8_t index,
                                   const std::vector<in_device_s>& devices) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto recorder = getMain<Recorder>();
  auto request = recorder.setDevicesToRecordRequest();
  request.setIndex(index);
  auto device_list = request.initDevices(devices.size());
  for (size_t i = 0; i < devices.size(); i++) {
    device_list[i].setFd(devices[i].fd);
    device_list[i].setName(devices[i].name);
    device_list[i].setType(devices[i].type);
  }
  waitWithTimeout(request.send());
}

std::vector<in_device_s> TestRunnerClient::listDevices(uint8_t index) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto recorder = getMain<Recorder>();
  auto request = recorder.listDevicesRequest();
  request.setIndex(index);
  auto response = waitWithTimeout(request.send());
  std::vector<in_device_s> device_list = {};
  for (const auto& dev_in : response.getDevices()) {
    in_device_s indev{};
    indev.fd = static_cast<int>(dev_in.getFd());
    strncpy(indev.name, dev_in.getName().cStr(), sizeof(indev.name));
    indev.name[sizeof(indev.name) - 1] = '\0';
    indev.type = (device_type)dev_in.getType();
    device_list.push_back(indev);
  }
  return device_list;
}

void TestRunnerClient::startCustomRecording(uint8_t index) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto recorder = getMain<Recorder>();
  auto request = recorder.startCustomRecordingRequest();
  request.setIndex(index);
  waitWithTimeout(request.send());
}

std::string TestRunnerClient::stopCustomRecording(uint8_t index) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto recorder = getMain<Recorder>();
  auto request = recorder.stopCustomRecordingRequest();
  request.setIndex(index);
  auto response = waitWithTimeout(request.send());
  return response.getPath();
}

bool TestRunnerClient::checkRecorderActive(uint8_t index) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto recorder = getMain<Recorder>();
  auto request = recorder.checkRecorderActiveRequest();
  request.setIndex(index);
  auto response = waitWithTimeout(request.send());
  return response.getActive();
}

void TestRunnerClient::WaitForTasks() {
  tasks.onEmpty().wait(getWaitScope());
}
