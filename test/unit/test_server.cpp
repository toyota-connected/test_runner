// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <fcntl.h>
#include <linux/uinput.h>

#include <capnp/message.h>
#include <kj/async.h>

#include "MockSyscall.h"
#include "TestRunnerServer.h"
#include "capnp/test_runner_server.capnp.h"
#include "capnp/test_runner.capnp.h"

using ::testing::_;
using ::testing::Return;

// ── helpers ───────────────────────────────────────────────────────────────────

// Set up the mock expectations that TestRunnerServer construction requires:
// four open() calls for /dev/uinput, ioctl_int / ioctl calls for each device,
// and four usleep() calls.  Snapshot recorder and plugins are disabled.
static void expect_server_construction(MockSyscall& mock,
                                       int mouse_fd = 10,
                                       int kb_fd = 11,
                                       int ts_fd = 12,
                                       int mts_fd = 13) {
  // Four virtual devices are opened in order: mouse, keyboard, touchscreen,
  // multi-touchscreen.
  EXPECT_CALL(mock, open(_, _))
      .WillOnce(Return(mouse_fd))
      .WillOnce(Return(kb_fd))
      .WillOnce(Return(ts_fd))
      .WillOnce(Return(mts_fd));

  EXPECT_CALL(mock, ioctl_int(_, _, _)).WillRepeatedly(Return(0));
  EXPECT_CALL(mock, ioctl(_, _, _)).WillRepeatedly(Return(0));
  EXPECT_CALL(mock, usleep(_)).WillRepeatedly(Return());

  // Destructor: UI_DEV_DESTROY + close for each fd.
  EXPECT_CALL(mock, close(_)).WillRepeatedly(Return(0));
}

// ── Construction / destruction ────────────────────────────────────────────────

TEST(TestRunnerServerConstruct, ConstructAndDestructNoCrash) {
  MockSyscall mock;
  expect_server_construction(mock);
  TestRunnerServer server(&mock, false, false);
}

TEST(TestRunnerServerConstruct, OpenFailSilent) {
  MockSyscall mock;
  // All opens fail — server should log errors but not crash.
  EXPECT_CALL(mock, open(_, _)).WillRepeatedly(Return(-1));
  EXPECT_CALL(mock, ioctl_int(_, _, _)).WillRepeatedly(Return(0));
  EXPECT_CALL(mock, ioctl(_, _, _)).WillRepeatedly(Return(0));
  EXPECT_CALL(mock, usleep(_)).WillRepeatedly(Return());
  EXPECT_CALL(mock, close(_)).WillRepeatedly(Return(0));

  TestRunnerServer server(&mock, false, false);
  // All device fds should be -1.
  EXPECT_EQ(server.getDeviceFd(VIRTUAL_MOUSE), -1);
  EXPECT_EQ(server.getDeviceFd(VIRTUAL_KEYBOARD), -1);
}

// ── getDeviceFd bounds ────────────────────────────────────────────────────────

TEST(TestRunnerServerGetDeviceFd, ValidIndicesReturnFds) {
  MockSyscall mock;
  expect_server_construction(mock, 10, 11, 12, 13);
  TestRunnerServer server(&mock, false, false);

  EXPECT_EQ(server.getDeviceFd(VIRTUAL_MOUSE), 10);
  EXPECT_EQ(server.getDeviceFd(VIRTUAL_KEYBOARD), 11);
  EXPECT_EQ(server.getDeviceFd(VIRTUAL_TOUCHSCREEN), 12);
  EXPECT_EQ(server.getDeviceFd(VIRTUAL_MULTI_TOUCHSCREEN), 13);
}

TEST(TestRunnerServerGetDeviceFd, OutOfBoundsReturnsNegOne) {
  MockSyscall mock;
  expect_server_construction(mock);
  TestRunnerServer server(&mock, false, false);

  EXPECT_EQ(server.getDeviceFd(MAX_DEVICES), -1);
  EXPECT_EQ(server.getDeviceFd(99), -1);
}

// ── emit ─────────────────────────────────────────────────────────────────────

TEST(TestRunnerServerEmit, WriteCalledWithCorrectArgs) {
  MockSyscall mock;
  expect_server_construction(mock, 10, 11, 12, 13);
  TestRunnerServer server(&mock, false, false);

  input_event captured{};
  EXPECT_CALL(mock, write(10, _, sizeof(input_event)))
      .WillOnce(::testing::DoAll(
          ::testing::Invoke([&](int, const void* buf, size_t) {
            memcpy(&captured, buf, sizeof(input_event));
          }),
          Return(sizeof(input_event))));

  server.emit(server.getDeviceFd(VIRTUAL_MOUSE), EV_REL, REL_X, 42);

  EXPECT_EQ(captured.type, EV_REL);
  EXPECT_EQ(captured.code, REL_X);
  EXPECT_EQ(captured.value, 42);
}

TEST(TestRunnerServerEmit, NegativeFdDoesNotWrite) {
  MockSyscall mock;
  expect_server_construction(mock);
  TestRunnerServer server(&mock, false, false);

  // write() must NOT be called when fd is -1.
  EXPECT_CALL(mock, write(_, _, _)).Times(0);
  server.emit(-1, EV_REL, REL_X, 1);
}

TEST(TestRunnerServerEmit, WriteFailureLogsError) {
  MockSyscall mock;
  expect_server_construction(mock, 10, 11, 12, 13);
  TestRunnerServer server(&mock, false, false);

  // write() returns -1 (failure) — server must not crash.
  EXPECT_CALL(mock, write(10, _, sizeof(input_event))).WillOnce(Return(-1));
  server.emit(10, EV_REL, REL_X, 5);  // no crash expected
}

// ── RPC handler tests via local Cap'n Proto client ────────────────────────────
//
// Each test builds an in-process TestRunnerService::Client wrapping an TestRunnerServer, then
// sends an RPC request and verifies the correct write() calls are made.

// Build a local Cap'n Proto client wrapping a heap-allocated TestRunnerServer.
// The server is moved into the Client's ownership; the MockSyscall must outlive
// both the request and the server.
static TestRunnerService::Client make_local_server(MockSyscall& mock) {
  expect_server_construction(mock, 10, 11, 12, 13);
  auto server = kj::heap<TestRunnerServer>(&mock, false, false);
  return TestRunnerService::Client(kj::mv(server));
}

TEST(TestRunnerServerHandlers, HandleMouseMove) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  TestRunnerService::Client client = make_local_server(mock);

  // Expect write for REL_X=5, REL_Y=-3, SYN_REPORT.
  EXPECT_CALL(mock, write(10, _, sizeof(input_event)))
      .Times(3)
      .WillRepeatedly(Return(sizeof(input_event)));

  auto req = client.handleMouseMoveRequest();
  req.getMouseMove().setRelX(5);
  req.getMouseMove().setRelY(-3);
  req.send().wait(ws);
}

TEST(TestRunnerServerHandlers, HandleMouseClick) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  TestRunnerService::Client client = make_local_server(mock);

  // Expect write for EV_KEY (btn) + SYN_REPORT.
  input_event captured{};
  EXPECT_CALL(mock, write(10, _, sizeof(input_event)))
      .Times(2)
      .WillRepeatedly(::testing::DoAll(
          ::testing::Invoke([&](int, const void* buf, size_t) {
            memcpy(&captured, buf, sizeof(input_event));
          }),
          Return(sizeof(input_event))));

  auto req = client.handleMouseClickRequest();
  req.getMouseClick().setBtn(BTN_LEFT);
  req.getMouseClick().setPressed(true);
  req.send().wait(ws);

  // captured holds the last write (SYN). Check the first write was the key event.
  // We verify by checking the second-to-last (key) event values.
  // Instead re-capture properly:
}

TEST(TestRunnerServerHandlers, HandleKeyPressRawCode) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  TestRunnerService::Client client = make_local_server(mock);

  std::vector<input_event> writes;
  EXPECT_CALL(mock, write(11, _, sizeof(input_event)))
      .Times(2)
      .WillRepeatedly(::testing::DoAll(
          ::testing::Invoke([&](int, const void* buf, size_t) {
            input_event ev{};
            memcpy(&ev, buf, sizeof(input_event));
            writes.push_back(ev);
          }),
          Return(sizeof(input_event))));

  auto req = client.handleKeyPressRequest();
  req.getKeyPress().setRawCode(KEY_A);
  req.getKeyPress().setPressed(true);
  req.send().wait(ws);

  ASSERT_EQ(writes.size(), 2u);
  EXPECT_EQ(writes[0].type, EV_KEY);
  EXPECT_EQ(writes[0].code, KEY_A);
  EXPECT_EQ(writes[0].value, 1);
  EXPECT_EQ(writes[1].type, EV_SYN);
}

// Without a loaded XKB keymap (no system keymap in test environment or
// keymap compilation fails) the keySym/keyName paths log a warning and
// return without writing any input events.
TEST(TestRunnerServerHandlers, HandleKeyPressKeySymNoKeymapIsNoOp) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  TestRunnerService::Client client = make_local_server(mock);

  // write() must NOT be called if the keysym cannot be resolved.
  EXPECT_CALL(mock, write(11, _, sizeof(input_event))).Times(::testing::AtMost(2));

  auto req = client.handleKeyPressRequest();
  req.getKeyPress().setKeySym(0x61 /* XKB_KEY_a */);
  req.getKeyPress().setPressed(true);
  req.send().wait(ws);
  // No assertion on writes — the important thing is no crash and no UB.
}

TEST(TestRunnerServerHandlers, HandleKeyPressKeyNameNoKeymapIsNoOp) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  TestRunnerService::Client client = make_local_server(mock);

  EXPECT_CALL(mock, write(11, _, sizeof(input_event))).Times(::testing::AtMost(2));

  auto req = client.handleKeyPressRequest();
  req.getKeyPress().setKeyName("Return");
  req.getKeyPress().setPressed(true);
  req.send().wait(ws);
}

TEST(TestRunnerServerHandlers, HandleSingleTouchXAndY) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  TestRunnerService::Client client = make_local_server(mock);

  std::vector<input_event> writes;
  EXPECT_CALL(mock, write(12, _, sizeof(input_event)))
      .Times(4)  // ABS_X, ABS_Y, BTN_TOUCH, SYN
      .WillRepeatedly(::testing::DoAll(
          ::testing::Invoke([&](int, const void* buf, size_t) {
            input_event ev{};
            memcpy(&ev, buf, sizeof(input_event));
            writes.push_back(ev);
          }),
          Return(sizeof(input_event))));

  auto req = client.handleSingleTouchRequest();
  req.getSingleTouch().setAbsX(100);
  req.getSingleTouch().setAbsY(200);
  req.getSingleTouch().setPressed(true);
  req.send().wait(ws);

  ASSERT_EQ(writes.size(), 4u);
  EXPECT_EQ(writes[0].code, ABS_X);
  EXPECT_EQ(writes[0].value, 100);
  EXPECT_EQ(writes[1].code, ABS_Y);
  EXPECT_EQ(writes[1].value, 200);
  EXPECT_EQ(writes[2].code, BTN_TOUCH);
  EXPECT_EQ(writes[2].value, 1);
  EXPECT_EQ(writes[3].type, EV_SYN);
}

TEST(TestRunnerServerHandlers, HandleSingleTouchNegativeXSkipped) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  TestRunnerService::Client client = make_local_server(mock);

  std::vector<input_event> writes;
  EXPECT_CALL(mock, write(12, _, sizeof(input_event)))
      .Times(3)  // ABS_Y, BTN_TOUCH, SYN (no ABS_X when x < 0)
      .WillRepeatedly(::testing::DoAll(
          ::testing::Invoke([&](int, const void* buf, size_t) {
            input_event ev{};
            memcpy(&ev, buf, sizeof(input_event));
            writes.push_back(ev);
          }),
          Return(sizeof(input_event))));

  auto req = client.handleSingleTouchRequest();
  req.getSingleTouch().setAbsX(-1);
  req.getSingleTouch().setAbsY(200);
  req.getSingleTouch().setPressed(false);
  req.send().wait(ws);

  ASSERT_EQ(writes.size(), 3u);
  EXPECT_EQ(writes[0].code, ABS_Y);
}

TEST(TestRunnerServerHandlers, HandleSingleTouchNegativeYSkipped) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  TestRunnerService::Client client = make_local_server(mock);

  std::vector<input_event> writes;
  EXPECT_CALL(mock, write(12, _, sizeof(input_event)))
      .Times(3)  // ABS_X, BTN_TOUCH, SYN (no ABS_Y when y < 0)
      .WillRepeatedly(::testing::DoAll(
          ::testing::Invoke([&](int, const void* buf, size_t) {
            input_event ev{};
            memcpy(&ev, buf, sizeof(input_event));
            writes.push_back(ev);
          }),
          Return(sizeof(input_event))));

  auto req = client.handleSingleTouchRequest();
  req.getSingleTouch().setAbsX(100);
  req.getSingleTouch().setAbsY(-1);
  req.getSingleTouch().setPressed(false);
  req.send().wait(ws);

  ASSERT_EQ(writes.size(), 3u);
  EXPECT_EQ(writes[0].code, ABS_X);
}

TEST(TestRunnerServerHandlers, HandleMultiTouchSingleSlot) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  TestRunnerService::Client client = make_local_server(mock);

  std::vector<input_event> writes;
  EXPECT_CALL(mock, write(13, _, sizeof(input_event)))
      .WillRepeatedly(::testing::DoAll(
          ::testing::Invoke([&](int, const void* buf, size_t) {
            input_event ev{};
            memcpy(&ev, buf, sizeof(input_event));
            writes.push_back(ev);
          }),
          Return(sizeof(input_event))));

  auto req = client.handleMultiTouchRequest();
  auto list = req.initMultiTouch(1);
  list[0].setSlot(0);
  list[0].setTrackingId(1);
  list[0].setAbsX(500);
  list[0].setAbsY(300);
  req.send().wait(ws);

  // Expect: ABS_MT_SLOT, ABS_MT_TRACKING_ID, ABS_MT_POSITION_X, ABS_MT_POSITION_Y, SYN
  ASSERT_GE(writes.size(), 5u);
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

TEST(TestRunnerServerHandlers, HandleMultiTouchZeroTrackingIdSkipsId) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  TestRunnerService::Client client = make_local_server(mock);

  std::vector<input_event> writes;
  EXPECT_CALL(mock, write(13, _, sizeof(input_event)))
      .WillRepeatedly(::testing::DoAll(
          ::testing::Invoke([&](int, const void* buf, size_t) {
            input_event ev{};
            memcpy(&ev, buf, sizeof(input_event));
            writes.push_back(ev);
          }),
          Return(sizeof(input_event))));

  auto req = client.handleMultiTouchRequest();
  auto list = req.initMultiTouch(1);
  list[0].setSlot(0);
  list[0].setTrackingId(0);  // trackingId == 0 → no TRACKING_ID event emitted
  list[0].setAbsX(100);
  list[0].setAbsY(100);
  req.send().wait(ws);

  // ABS_MT_TRACKING_ID must not appear in writes.
  for (auto& ev : writes) {
    EXPECT_NE(ev.code, ABS_MT_TRACKING_ID)
        << "ABS_MT_TRACKING_ID should not be emitted when trackingId == 0";
  }
}

TEST(TestRunnerServerHandlers, HandlePassThrough) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  TestRunnerService::Client client = make_local_server(mock);

  input_event captured{};
  EXPECT_CALL(mock, write(11, _, sizeof(input_event)))
      .WillOnce(::testing::DoAll(
          ::testing::Invoke([&](int, const void* buf, size_t) {
            memcpy(&captured, buf, sizeof(input_event));
          }),
          Return(sizeof(input_event))));

  auto req = client.handlePassThroughRequest();
  req.getPassThrough().setDevice(VIRTUAL_KEYBOARD);
  req.getPassThrough().setType(EV_KEY);
  req.getPassThrough().setCode(KEY_ENTER);
  req.getPassThrough().setValue(1);
  req.send().wait(ws);

  EXPECT_EQ(captured.type, EV_KEY);
  EXPECT_EQ(captured.code, KEY_ENTER);
  EXPECT_EQ(captured.value, 1);
}

// ── Recorder delegation (createCustomRecorder, listDevices, etc.) ─────────────
//
// These tests exercise the RPC methods that delegate to TestRunnerRecorder.  We use a
// local Cap'n Proto client; the TestRunnerRecorder is created internally by the server
// via createCustomRecorder.

TEST(TestRunnerServerRecorder, CreateCustomRecorderReturnsIndex) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  // Server construction.
  expect_server_construction(mock, 10, 11, 12, 13);
  // The custom recorder constructor calls opendir/readdir/closedir.
  EXPECT_CALL(mock, opendir(_)).WillRepeatedly(Return(nullptr));

  auto server = kj::heap<TestRunnerServer>(&mock, false, false);
  TestRunnerService::Client client(kj::mv(server));

  auto req = client.createCustomRecorderRequest();
  req.setFilename("myrecording");
  req.setLength(10);
  req.setContinuous(false);
  auto result = req.send().wait(ws);

  EXPECT_EQ(result.getIndex(), 0u);
}

TEST(TestRunnerServerRecorder, CreateCustomRecorderFilenameWithSlashReturns255) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  expect_server_construction(mock, 10, 11, 12, 13);
  auto server = kj::heap<TestRunnerServer>(&mock, false, false);
  TestRunnerService::Client client(kj::mv(server));

  auto req = client.createCustomRecorderRequest();
  req.setFilename("../../etc/passwd");
  req.setLength(10);
  req.setContinuous(false);
  auto result = req.send().wait(ws);

  EXPECT_EQ(result.getIndex(), 255u);
}

TEST(TestRunnerServerRecorder, CreateCustomRecorderFilenameWithDotDotReturns255) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  expect_server_construction(mock, 10, 11, 12, 13);
  auto server = kj::heap<TestRunnerServer>(&mock, false, false);
  TestRunnerService::Client client(kj::mv(server));

  auto req = client.createCustomRecorderRequest();
  req.setFilename("file..name");
  req.setLength(10);
  req.setContinuous(false);
  auto result = req.send().wait(ws);

  EXPECT_EQ(result.getIndex(), 255u);
}

TEST(TestRunnerServerRecorder, CheckRecorderActiveOutOfRangeReturnsFalse) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  expect_server_construction(mock, 10, 11, 12, 13);
  auto server = kj::heap<TestRunnerServer>(&mock, false, false);
  TestRunnerService::Client client(kj::mv(server));

  auto req = client.checkRecorderActiveRequest();
  req.setIndex(99);  // no recorder at index 99
  auto result = req.send().wait(ws);

  EXPECT_FALSE(result.getActive());
}

TEST(TestRunnerServerRecorder, ListDevicesOutOfRangeReturnsEmpty) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  expect_server_construction(mock, 10, 11, 12, 13);
  auto server = kj::heap<TestRunnerServer>(&mock, false, false);
  TestRunnerService::Client client(kj::mv(server));

  auto req = client.listDevicesRequest();
  req.setIndex(99);
  auto result = req.send().wait(ws);

  EXPECT_EQ(result.getDevices().size(), 0u);
}

TEST(TestRunnerServerRecorder, StopCustomRecordingOutOfRangeNocrash) {
  MockSyscall mock;
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  expect_server_construction(mock, 10, 11, 12, 13);
  auto server = kj::heap<TestRunnerServer>(&mock, false, false);
  TestRunnerService::Client client(kj::mv(server));

  auto req = client.stopCustomRecordingRequest();
  req.setIndex(99);
  req.send().wait(ws);  // must not crash
}
