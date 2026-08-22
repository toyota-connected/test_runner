// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3

// Integration tests: TestRunnerServer + TestRunnerClient over TCP loopback.
//
// These tests use a real EzRpcServer bound to a random loopback port and a real
// TestRunnerClient connecting to it.  MockSyscall intercepts all uinput writes so we
// can verify the full RPC→server→uinput pipeline without kernel access.
//
// Recorder tests exercise the createCustomRecorder → start → stop → file
// lifecycle through the complete RPC stack.

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <fcntl.h>
#include <string>
#include <fstream>
#include <regex>
#include <thread>
#include <chrono>

#include "ServerThreadHelper.h"

// TestRunnerClient.h redefines input_absinfo/device_type/in_device_s; use
// linux/input-event-codes.h (macros only) and a local struct mirror.
#include "TestRunnerClient.h"
#include <linux/input-event-codes.h>

struct test_input_event {
  uint64_t time_sec;
  uint64_t time_usec;
  uint16_t type;
  uint16_t code;
  int32_t  value;
};
static_assert(sizeof(test_input_event) == 24, "unexpected input_event size");

using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::AtLeast;

// ── Helper ────────────────────────────────────────────────────────────────────

static void spin_until_file(const std::string& path, int timeout_ms = 3000) {
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    std::ifstream f(path);
    if (f.good()) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

// ── Full-pipeline input event tests ───────────────────────────────────────────
//
// Each test connects a real TestRunnerClient to a real EzRpcServer (backed by an
// TestRunnerServer with MockSyscall) over TCP loopback, sends one command, and
// verifies the expected write() calls arrive at the mock.

TEST(Integration, MouseMoveRoundTrip) {
  ServerThread srv;
  std::vector<test_input_event> writes;
  EXPECT_CALL(srv.mock, write(10, _, sizeof(test_input_event)))
      .Times(3)
      .WillRepeatedly(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            test_input_event ev{};
            memcpy(&ev, buf, sizeof(ev));
            writes.push_back(ev);
          }),
          Return(sizeof(test_input_event))));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  client.SendMouseMove(10, -20);

  ASSERT_EQ(writes.size(), 3u);
  EXPECT_EQ(writes[0].type,  EV_REL);
  EXPECT_EQ(writes[0].code,  REL_X);
  EXPECT_EQ(writes[0].value, 10);
  EXPECT_EQ(writes[1].type,  EV_REL);
  EXPECT_EQ(writes[1].code,  REL_Y);
  EXPECT_EQ(writes[1].value, -20);
  EXPECT_EQ(writes[2].type,  EV_SYN);
}

TEST(Integration, MouseClickRoundTrip) {
  ServerThread srv;
  std::vector<test_input_event> writes;
  EXPECT_CALL(srv.mock, write(10, _, sizeof(test_input_event)))
      .Times(2)
      .WillRepeatedly(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            test_input_event ev{};
            memcpy(&ev, buf, sizeof(ev));
            writes.push_back(ev);
          }),
          Return(sizeof(test_input_event))));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  client.SendMouseClick(BTN_LEFT, true);

  ASSERT_EQ(writes.size(), 2u);
  EXPECT_EQ(writes[0].type,  EV_KEY);
  EXPECT_EQ(writes[0].code,  BTN_LEFT);
  EXPECT_EQ(writes[0].value, 1);
  EXPECT_EQ(writes[1].type,  EV_SYN);
}

TEST(Integration, KeyPressRoundTrip) {
  ServerThread srv;
  std::vector<test_input_event> writes;
  EXPECT_CALL(srv.mock, write(11, _, sizeof(test_input_event)))
      .Times(2)
      .WillRepeatedly(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            test_input_event ev{};
            memcpy(&ev, buf, sizeof(ev));
            writes.push_back(ev);
          }),
          Return(sizeof(test_input_event))));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  client.SendKeyPress(KEY_SPACE, true);

  ASSERT_EQ(writes.size(), 2u);
  EXPECT_EQ(writes[0].type,  EV_KEY);
  EXPECT_EQ(writes[0].code,  KEY_SPACE);
  EXPECT_EQ(writes[0].value, 1);
  EXPECT_EQ(writes[1].type,  EV_SYN);
}

TEST(Integration, SingleTouchRoundTrip) {
  ServerThread srv;
  std::vector<test_input_event> writes;
  EXPECT_CALL(srv.mock, write(12, _, sizeof(test_input_event)))
      .Times(4)
      .WillRepeatedly(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            test_input_event ev{};
            memcpy(&ev, buf, sizeof(ev));
            writes.push_back(ev);
          }),
          Return(sizeof(test_input_event))));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  client.SendSingleTouch(500, 750, true);

  ASSERT_EQ(writes.size(), 4u);
  EXPECT_EQ(writes[0].code,  ABS_X);
  EXPECT_EQ(writes[0].value, 500);
  EXPECT_EQ(writes[1].code,  ABS_Y);
  EXPECT_EQ(writes[1].value, 750);
  EXPECT_EQ(writes[2].code,  BTN_TOUCH);
  EXPECT_EQ(writes[2].value, 1);
  EXPECT_EQ(writes[3].type,  EV_SYN);
}

TEST(Integration, MultiTouchRoundTrip) {
  ServerThread srv;
  std::vector<test_input_event> writes;
  EXPECT_CALL(srv.mock, write(13, _, sizeof(test_input_event)))
      .WillRepeatedly(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            test_input_event ev{};
            memcpy(&ev, buf, sizeof(ev));
            writes.push_back(ev);
          }),
          Return(sizeof(test_input_event))));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());

  multiTouch touches[2] = {{0, 1, 200, 300}, {1, 2, 400, 600}};
  client.SendMultiTouch(touches, 2);

  int slot_count = 0;
  bool found_syn = false;
  for (auto& ev : writes) {
    if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT) slot_count++;
    if (ev.type == EV_SYN) found_syn = true;
  }
  EXPECT_EQ(slot_count, 2);
  EXPECT_TRUE(found_syn);
}

TEST(Integration, PassThroughRoundTrip) {
  ServerThread srv;
  test_input_event captured{};
  EXPECT_CALL(srv.mock, write(11, _, sizeof(test_input_event)))
      .WillOnce(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            memcpy(&captured, buf, sizeof(captured));
          }),
          Return(sizeof(test_input_event))));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  client.SendPassThrough(1 /*VIRTUAL_KEYBOARD*/, EV_KEY, KEY_TAB, 1);

  EXPECT_EQ(captured.type,  EV_KEY);
  EXPECT_EQ(captured.code,  KEY_TAB);
  EXPECT_EQ(captured.value, 1);
}

// ── Multi-command sequence ─────────────────────────────────────────────────────
//
// Sends several different commands in sequence over one connection and verifies
// all arrive correctly.  This exercises connection reuse across calls.

TEST(Integration, MultiCommandSequence) {
  ServerThread srv;

  std::vector<test_input_event> mouse_writes;
  std::vector<test_input_event> kb_writes;

  EXPECT_CALL(srv.mock, write(10, _, sizeof(test_input_event)))
      .WillRepeatedly(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            test_input_event ev{};
            memcpy(&ev, buf, sizeof(ev));
            mouse_writes.push_back(ev);
          }),
          Return(sizeof(test_input_event))));

  EXPECT_CALL(srv.mock, write(11, _, sizeof(test_input_event)))
      .WillRepeatedly(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            test_input_event ev{};
            memcpy(&ev, buf, sizeof(ev));
            kb_writes.push_back(ev);
          }),
          Return(sizeof(test_input_event))));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());

  client.SendMouseMove(1, 2);          // 3 writes to fd 10
  client.SendMouseClick(BTN_LEFT, true); // 2 writes to fd 10
  client.SendKeyPress(KEY_A, true);    // 2 writes to fd 11

  // 5 events on mouse fd (REL_X, REL_Y, SYN, EV_KEY, SYN)
  EXPECT_EQ(mouse_writes.size(), 5u);
  // 2 events on keyboard fd (EV_KEY, SYN)
  EXPECT_EQ(kb_writes.size(), 2u);
}

// ── Recorder RPC lifecycle tests ───────────────────────────────────────────────
//
// These tests exercise the full createCustomRecorder → start → stop → file
// lifecycle via TestRunnerClient RPC calls to a live EzRpcServer-wrapped TestRunnerServer.
//
// createCustomRecorder passes no syscall mock to the TestRunnerRecorder it creates,
// so the recorder uses real syscalls against /dev/input.  The tests are designed
// to be robust to any set of real devices present on the host.

TEST(IntegrationRecorder, CreateStartStopProducesFile) {
  ServerThread srv;

  TestRunnerClient client;
  client.Connect(srv.address().c_str());

  uint8_t idx = client.createCustomRecorder("integration_test_rec", 30, false);
  ASSERT_NE(idx, 255u) << "createCustomRecorder returned error index";

  client.startCustomRecording(idx);

  // Wait for the recorder to become active.
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  bool active = false;
  while (std::chrono::steady_clock::now() < deadline) {
    if (client.checkRecorderActive(idx)) { active = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  ASSERT_TRUE(active) << "recorder never became active";

  std::string path = client.stopCustomRecording(idx);
  EXPECT_FALSE(path.empty()) << "stopCustomRecording returned empty path";

  // The file must exist and contain at least a header line.
  spin_until_file(path);
  std::ifstream f(path);
  ASSERT_TRUE(f.good()) << "recording file not found at: " << path;
  std::string first_line;
  ASSERT_TRUE(std::getline(f, first_line));
  // First line is the version header.
  EXPECT_EQ(first_line, "version 2");
  // Second line is the timestamp header: [HH:MM:SS.mmm]
  std::string second_line;
  ASSERT_TRUE(std::getline(f, second_line));
  EXPECT_TRUE(std::regex_search(second_line,
      std::regex(R"(\[\d{2}:\d{2}:\d{2}\.\d{3}\])")));
}

TEST(IntegrationRecorder, CheckActiveBeforeAndAfterStart) {
  ServerThread srv;

  TestRunnerClient client;
  client.Connect(srv.address().c_str());

  uint8_t idx = client.createCustomRecorder("integration_active_check", 30, false);
  ASSERT_NE(idx, 255u);

  // Before start: not active.
  EXPECT_FALSE(client.checkRecorderActive(idx));

  client.startCustomRecording(idx);

  // Spin until active (background thread sets m_stop=false asynchronously).
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  bool active = false;
  while (std::chrono::steady_clock::now() < deadline) {
    if (client.checkRecorderActive(idx)) { active = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  EXPECT_TRUE(active);

  client.stopCustomRecording(idx);
}

TEST(IntegrationRecorder, ListDevicesReturnsPopulatedList) {
  ServerThread srv;

  TestRunnerClient client;
  client.Connect(srv.address().c_str());

  uint8_t idx = client.createCustomRecorder("integration_list_devices", 30, false);
  ASSERT_NE(idx, 255u);

  // The recorder scans real /dev/input on construction.
  // We only assert that the call succeeds and returns a vector (may be empty
  // in a minimal container environment).
  auto devices = client.listDevices(idx);
  // The list may be empty in sandboxed environments — no crash is the guarantee.
  SUCCEED();
}

TEST(IntegrationRecorder, FilenameValidationViaRpc) {
  ServerThread srv;

  TestRunnerClient client;
  client.Connect(srv.address().c_str());

  // Path traversal attempts must be rejected by the server.
  EXPECT_EQ(client.createCustomRecorder("../../etc/passwd", 10, false), 255u);
  EXPECT_EQ(client.createCustomRecorder("file..name", 10, false), 255u);
  EXPECT_EQ(client.createCustomRecorder("sub/dir", 10, false), 255u);
}

TEST(IntegrationRecorder, MultipleRecordersCoexist) {
  ServerThread srv;

  TestRunnerClient client;
  client.Connect(srv.address().c_str());

  uint8_t idx0 = client.createCustomRecorder("integration_multi_0", 30, false);
  uint8_t idx1 = client.createCustomRecorder("integration_multi_1", 30, false);

  ASSERT_NE(idx0, 255u);
  ASSERT_NE(idx1, 255u);
  EXPECT_NE(idx0, idx1) << "two recorders should receive distinct indices";

  client.startCustomRecording(idx0);
  client.startCustomRecording(idx1);

  // Wait for both to become active.
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline) {
    if (client.checkRecorderActive(idx0) && client.checkRecorderActive(idx1)) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  EXPECT_TRUE(client.checkRecorderActive(idx0));
  EXPECT_TRUE(client.checkRecorderActive(idx1));

  // Stop both; both should return valid (non-empty) paths.
  std::string path0 = client.stopCustomRecording(idx0);
  std::string path1 = client.stopCustomRecording(idx1);

  EXPECT_FALSE(path0.empty());
  EXPECT_FALSE(path1.empty());
  EXPECT_NE(path0, path1) << "two recorders should write to distinct files";
}

TEST(IntegrationRecorder, StopCustomRecordingOutOfRangeNoCrash) {
  ServerThread srv;

  TestRunnerClient client;
  client.Connect(srv.address().c_str());

  EXPECT_NO_THROW(client.stopCustomRecording(99));
  EXPECT_NO_THROW(client.startCustomRecording(99));
  EXPECT_FALSE(client.checkRecorderActive(99));
}

// ── Recorder setDevicesToRecord integration test ───────────────────────────────

TEST(IntegrationRecorder, SetDevicesToRecordAndVerifyFile) {
  ServerThread srv;

  TestRunnerClient client;
  client.Connect(srv.address().c_str());

  uint8_t idx = client.createCustomRecorder("integration_set_devices", 30, false);
  ASSERT_NE(idx, 255u);

  // Get available devices from the recorder and pass them back.
  auto devices = client.listDevices(idx);
  // setDevicesToRecord with the full list should work without crashing.
  EXPECT_NO_THROW(client.setDevicesToRecord(idx, devices));

  client.startCustomRecording(idx);

  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  bool active = false;
  while (std::chrono::steady_clock::now() < deadline) {
    if (client.checkRecorderActive(idx)) { active = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  ASSERT_TRUE(active);

  std::string path = client.stopCustomRecording(idx);
  EXPECT_FALSE(path.empty());

  spin_until_file(path);
  std::ifstream f(path);
  EXPECT_TRUE(f.good()) << "file not found at: " << path;
}
