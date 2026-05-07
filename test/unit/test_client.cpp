// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <fcntl.h>
#include <string>

#include "ServerThreadHelper.h"

// TestRunnerClient.h defines its own input_absinfo/device_type/in_device_s to avoid
// dragging linux/input.h into its public API.  We include linux/input-event-codes.h
// for the EV_*/KEY_*/ABS_*/BTN_*/REL_* macros (no struct definitions), and define
// a local mirror of input_event (struct layout is stable across kernel versions).
#include "TestRunnerClient.h"
#include <linux/input-event-codes.h>

// Mirror of struct input_event from linux/input.h (cannot include that header
// alongside TestRunnerClient.h due to duplicate input_absinfo definition).
struct test_input_event {
  uint64_t time_sec;
  uint64_t time_usec;
  uint16_t type;
  uint16_t code;
  int32_t value;
};
static_assert(sizeof(test_input_event) == 24, "unexpected input_event size");

// Device index constants from TestRunnerServer.h (not included here to avoid conflicts)
static constexpr int VIRTUAL_KEYBOARD_IDX = 1;

using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::Invoke;

// ── TestRunnerClient Send* tests ──────────────────────────────────────────────────────

TEST(TestRunnerClientSend, SendMouseMove) {
  ServerThread srv;
  std::vector<test_input_event> writes;
  EXPECT_CALL(srv.mock, write(10, _, sizeof(test_input_event)))
      .Times(3)
      .WillRepeatedly(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            test_input_event ev{};
            memcpy(&ev, buf, sizeof(test_input_event));
            writes.push_back(ev);
          }),
          Return(sizeof(test_input_event))));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  client.SendMouseMove(5, -3);

  ASSERT_EQ(writes.size(), 3u);
  EXPECT_EQ(writes[0].type, EV_REL);
  EXPECT_EQ(writes[0].code, REL_X);
  EXPECT_EQ(writes[0].value, 5);
  EXPECT_EQ(writes[1].type, EV_REL);
  EXPECT_EQ(writes[1].code, REL_Y);
  EXPECT_EQ(writes[1].value, -3);
  EXPECT_EQ(writes[2].type, EV_SYN);
}

TEST(TestRunnerClientSend, SendMouseClick) {
  ServerThread srv;
  std::vector<test_input_event> writes;
  EXPECT_CALL(srv.mock, write(10, _, sizeof(test_input_event)))
      .Times(2)
      .WillRepeatedly(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            test_input_event ev{};
            memcpy(&ev, buf, sizeof(test_input_event));
            writes.push_back(ev);
          }),
          Return(sizeof(test_input_event))));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  client.SendMouseClick(BTN_RIGHT, true);

  ASSERT_EQ(writes.size(), 2u);
  EXPECT_EQ(writes[0].type, EV_KEY);
  EXPECT_EQ(writes[0].code, BTN_RIGHT);
  EXPECT_EQ(writes[0].value, 1);
  EXPECT_EQ(writes[1].type, EV_SYN);
}

TEST(TestRunnerClientSend, SendKeyPress) {
  ServerThread srv;
  std::vector<test_input_event> writes;
  EXPECT_CALL(srv.mock, write(11, _, sizeof(test_input_event)))
      .Times(2)
      .WillRepeatedly(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            test_input_event ev{};
            memcpy(&ev, buf, sizeof(test_input_event));
            writes.push_back(ev);
          }),
          Return(sizeof(test_input_event))));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  client.SendKeyPress(KEY_ENTER, false);

  ASSERT_EQ(writes.size(), 2u);
  EXPECT_EQ(writes[0].type, EV_KEY);
  EXPECT_EQ(writes[0].code, KEY_ENTER);
  EXPECT_EQ(writes[0].value, 0);
  EXPECT_EQ(writes[1].type, EV_SYN);
}

TEST(TestRunnerClientSend, SendSingleTouch) {
  ServerThread srv;
  std::vector<test_input_event> writes;
  EXPECT_CALL(srv.mock, write(12, _, sizeof(test_input_event)))
      .Times(4)
      .WillRepeatedly(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            test_input_event ev{};
            memcpy(&ev, buf, sizeof(test_input_event));
            writes.push_back(ev);
          }),
          Return(sizeof(test_input_event))));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  client.SendSingleTouch(100, 200, true);

  ASSERT_EQ(writes.size(), 4u);
  EXPECT_EQ(writes[0].code, ABS_X);
  EXPECT_EQ(writes[0].value, 100);
  EXPECT_EQ(writes[1].code, ABS_Y);
  EXPECT_EQ(writes[1].value, 200);
  EXPECT_EQ(writes[2].code, BTN_TOUCH);
  EXPECT_EQ(writes[2].value, 1);
  EXPECT_EQ(writes[3].type, EV_SYN);
}

TEST(TestRunnerClientSend, SendMultiTouchSingleSlot) {
  ServerThread srv;
  std::vector<test_input_event> writes;
  EXPECT_CALL(srv.mock, write(13, _, sizeof(test_input_event)))
      .WillRepeatedly(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            test_input_event ev{};
            memcpy(&ev, buf, sizeof(test_input_event));
            writes.push_back(ev);
          }),
          Return(sizeof(test_input_event))));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());

  multiTouch touches[1] = {{0, 1, 500, 300}};
  client.SendMultiTouch(touches, 1);

  bool found_slot = false, found_x = false, found_syn = false;
  for (auto& ev : writes) {
    if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT) found_slot = true;
    if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_X) found_x = true;
    if (ev.type == EV_SYN) found_syn = true;
  }
  EXPECT_TRUE(found_slot);
  EXPECT_TRUE(found_x);
  EXPECT_TRUE(found_syn);
}

TEST(TestRunnerClientSend, SendMultiTouchTwoSlots) {
  ServerThread srv;
  std::vector<test_input_event> writes;
  EXPECT_CALL(srv.mock, write(13, _, sizeof(test_input_event)))
      .WillRepeatedly(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            test_input_event ev{};
            memcpy(&ev, buf, sizeof(test_input_event));
            writes.push_back(ev);
          }),
          Return(sizeof(test_input_event))));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());

  multiTouch touches[2] = {{0, 1, 100, 200}, {1, 2, 300, 400}};
  client.SendMultiTouch(touches, 2);

  int slot_count = 0;
  for (auto& ev : writes) {
    if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT) slot_count++;
  }
  EXPECT_EQ(slot_count, 2);
}

TEST(TestRunnerClientSend, SendPassThrough) {
  ServerThread srv;
  std::vector<test_input_event> writes;
  EXPECT_CALL(srv.mock, write(11, _, sizeof(test_input_event)))
      .Times(1)
      .WillRepeatedly(DoAll(
          Invoke([&](int, const void* buf, size_t) {
            test_input_event ev{};
            memcpy(&ev, buf, sizeof(test_input_event));
            writes.push_back(ev);
          }),
          Return(sizeof(test_input_event))));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  client.SendPassThrough(VIRTUAL_KEYBOARD_IDX, EV_KEY, KEY_ESC, 1);

  ASSERT_EQ(writes.size(), 1u);
  EXPECT_EQ(writes[0].type, EV_KEY);
  EXPECT_EQ(writes[0].code, KEY_ESC);
  EXPECT_EQ(writes[0].value, 1);
}

TEST(TestRunnerClientNotConnected, CreateCustomRecorderThrows) {
  TestRunnerClient client;
  EXPECT_THROW(client.createCustomRecorder("test", 10, false), kj::Exception);
}

// ── TestRunnerClient recorder delegation tests ───────────────────────────────────────

TEST(TestRunnerClientRecorder, CreateCustomRecorderReturnsIndex) {
  ServerThread srv;
  EXPECT_CALL(srv.mock, opendir(_)).WillRepeatedly(Return(nullptr));

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  uint8_t idx = client.createCustomRecorder("myrecording", 10, false);

  EXPECT_EQ(idx, 0u);
}

TEST(TestRunnerClientRecorder, CreateCustomRecorderSlashInFilenameReturns255) {
  ServerThread srv;

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  uint8_t idx = client.createCustomRecorder("../../etc/passwd", 10, false);

  EXPECT_EQ(idx, 255u);
}

TEST(TestRunnerClientRecorder, CheckRecorderActiveOutOfRangeReturnsFalse) {
  ServerThread srv;

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  bool active = client.checkRecorderActive(99);

  EXPECT_FALSE(active);
}

TEST(TestRunnerClientRecorder, ListDevicesOutOfRangeReturnsEmpty) {
  ServerThread srv;

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  auto devices = client.listDevices(99);

  EXPECT_EQ(devices.size(), 0u);
}

TEST(TestRunnerClientRecorder, StopCustomRecordingOutOfRangeNoCrash) {
  ServerThread srv;

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  // Must not throw or crash for out-of-range index.
  EXPECT_NO_THROW(client.stopCustomRecording(99));
}

TEST(TestRunnerClientRecorder, StartCustomRecordingOutOfRangeNoCrash) {
  ServerThread srv;

  TestRunnerClient client;
  client.Connect(srv.address().c_str());
  EXPECT_NO_THROW(client.startCustomRecording(99));
}

// ── RPCClient::Connect tests ──────────────────────────────────────────────────

TEST(RPCClientConnect, SendSucceedsAfterConnect) {
  ServerThread srv;
  EXPECT_CALL(srv.mock, write(10, _, sizeof(test_input_event)))
      .WillRepeatedly(Return(sizeof(test_input_event)));

  TestRunnerClient client;
  // Before Connect, Send throws due to isConnected == false.
  EXPECT_THROW(client.SendMouseMove(1, 2), kj::Exception);
  // After Connect, Send succeeds.
  client.Connect(srv.address().c_str());
  EXPECT_NO_THROW(client.SendMouseMove(1, 2));
}
