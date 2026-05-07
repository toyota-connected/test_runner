// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <dirent.h>
#include <linux/input.h>
#include <sys/select.h>
#include <chrono>
#include <fstream>
#include <regex>
#include <thread>

#include "MockSyscall.h"
#include "TestRunnerRecorder.h"

// ── Test peer — accesses private members/methods ──────────────────────────────
class TestRunnerRecorderTestPeer {
 public:
  explicit TestRunnerRecorderTestPeer(TestRunnerRecorder& r) : r_(r) {}

  // Expose private static method
  static void transform(in_device_s dev, int& val, int axis) {
    TestRunnerRecorder::transform_touch_coordinates(dev, val, axis);
  }

  std::string current_time_str(std::chrono::milliseconds ms) {
    return r_.current_time_str(ms);
  }

  bool is_done() { return r_.is_done(); }

  void set_m_stop(bool v) { r_.m_stop.store(v, std::memory_order_release); }
  void set_m_continuous(bool v) { r_.m_continuous = v; }
  void set_m_start_time(std::chrono::milliseconds t) { r_.m_start_time = t; }
  void set_m_recording_length(uint32_t len) { r_.m_recording_length = len; }
  uint64_t get_device_mask() const { return r_.m_device_mask; }
  std::chrono::milliseconds current_time_ms() { return r_.current_time_ms(); }

 private:
  TestRunnerRecorder& r_;
};

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::ReturnNull;
using ::testing::SetArgPointee;

// ── helpers ───────────────────────────────────────────────────────────────────

static struct dirent make_dirent(const char* name, unsigned char type) {
  struct dirent d{};
  d.d_type = type;
  strncpy(d.d_name, name, sizeof(d.d_name) - 1);
  return d;
}

// Sentinel pointer used as a fake DIR* (the value is never dereferenced by
// TestRunnerRecorder; it just passes it back to readdir/closedir).
static char fake_dir_storage[1];
static DIR* const kFakeDir = reinterpret_cast<DIR*>(fake_dir_storage);

// Minimal mock that returns an empty /dev/input directory.
static void expect_empty_dir(MockSyscall& mock) {
  EXPECT_CALL(mock, opendir(_)).WillRepeatedly(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir)).WillRepeatedly(ReturnNull());
  EXPECT_CALL(mock, closedir(kFakeDir)).WillRepeatedly(Return(0));
}

// ── TestRunnerRecorder construction ──────────────────────────────────────────────────

TEST(TestRunnerRecorderConstruct, EmptyDirNoCrash) {
  MockSyscall mock;
  expect_empty_dir(mock);
  // Constructor should complete without crash even with no devices.
  TestRunnerRecorder rec((char*)"test_rec", 30, false, "/fake/input", &mock);
}

TEST(TestRunnerRecorderConstruct, ContinuousModeNoCrash) {
  MockSyscall mock;
  expect_empty_dir(mock);
  TestRunnerRecorder rec((char*)"test_cont", 10, true, "/fake/input", &mock);
}

TEST(TestRunnerRecorderConstruct, OpendirFailNoCrash) {
  MockSyscall mock;
  EXPECT_CALL(mock, opendir(_)).WillOnce(ReturnNull());
  TestRunnerRecorder rec((char*)"test_fail", 30, false, "/nonexistent", &mock);
}

// ── listDevices / setDevicesToRecord ──────────────────────────────────────────

TEST(TestRunnerRecorderDevices, ListDevicesEmptyWhenNoneFound) {
  MockSyscall mock;
  expect_empty_dir(mock);
  TestRunnerRecorder rec((char*)"test_list", 30, false, "/fake/input", &mock);
  EXPECT_TRUE(rec.listDevices().empty());
}

TEST(TestRunnerRecorderDevices, SetDevicesToRecordEmptyList) {
  MockSyscall mock;
  expect_empty_dir(mock);
  TestRunnerRecorder rec((char*)"test_set", 30, false, "/fake/input", &mock);
  // Should not crash with empty device list.
  rec.setDevicesToRecord({});
}

// ── isActive ──────────────────────────────────────────────────────────────────

TEST(TestRunnerRecorderActive, NotActiveBeforeStart) {
  MockSyscall mock;
  expect_empty_dir(mock);
  TestRunnerRecorder rec((char*)"test_active", 30, false, "/fake/input", &mock);
  // Recorder is stopped (m_stop=true) until Start() is called.
  EXPECT_FALSE(rec.isActive());
}


// ── Destructor closes all open fds ───────────────────────────────────────────

TEST(TestRunnerRecorderDestruct, ClosesFdsOnDestruction) {
  MockSyscall mock;

  static struct dirent event0 = make_dirent("event0", DT_CHR);
  static struct dirent* seq[] = {&event0, nullptr};
  int seq_idx = 0;

  EXPECT_CALL(mock, opendir(_)).WillOnce(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir)).WillRepeatedly(Invoke([&](DIR*) -> struct dirent* {
    return seq[seq_idx < 2 ? seq_idx++ : 1];
  }));
  EXPECT_CALL(mock, closedir(kFakeDir)).WillOnce(Return(0));

  // open() called for the device file.
  EXPECT_CALL(mock, open(_, _)).WillOnce(Return(42));

  // EVIOCGNAME — return a device name.
  EXPECT_CALL(mock, ioctl(42, EVIOCGNAME(50), _))
      .WillOnce(DoAll(Invoke([](int, unsigned long, void* arg) {
                        strncpy(static_cast<char*>(arg), "test_kb", 50);
                      }),
                      Return(7)));

  // Capability bits: only EV_KEY set (byte index 1, bit 1).
  uint8_t caps[150]{};
  caps[EV_KEY / 8] = (1 << (EV_KEY % 8));
  EXPECT_CALL(mock, ioctl(42, EVIOCGBIT(0, 150), _))
      .WillOnce(DoAll(Invoke([caps](int, unsigned long, void* arg) {
                        memcpy(arg, caps, sizeof(caps));
                      }),
                      Return(sizeof(caps))));

  // ABS capabilities — none.
  EXPECT_CALL(mock, ioctl(42, EVIOCGBIT(EV_ABS, 150), _))
      .WillOnce(DoAll(Invoke([](int, unsigned long, void* arg) {
                        memset(arg, 0, 150);
                      }),
                      Return(150)));

  // Destructor must close the fd.
  EXPECT_CALL(mock, close(42)).WillOnce(Return(0));

  {
    TestRunnerRecorder rec((char*)"test_close", 30, false, "/fake/input", &mock);
    ASSERT_EQ(rec.listDevices().size(), 1u);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 2 — Pure-logic tests (no mocking required)
// ═══════════════════════════════════════════════════════════════════════════════

// ── transform_touch_coordinates ──────────────────────────────────────────────

// Helper: build a device with given abs ranges.
static in_device_s make_device(int x_min, int x_max, int y_min, int y_max) {
  in_device_s d{};
  d.abs_range_x.minimum = x_min;
  d.abs_range_x.maximum = x_max;
  d.abs_range_y.minimum = y_min;
  d.abs_range_y.maximum = y_max;
  return d;
}

TEST(TransformTouchCoords, XAxisScale) {
  // Device range [0, 1000] → VIRTUAL_MULTITOUCH_ABS_X_MAX (16000)
  auto dev = make_device(0, 1000, 0, 1000);
  int val = 500;
  TestRunnerRecorderTestPeer::transform(dev, val, X_AXIS);
  // 500 * (16000 / 1000) = 8000
  EXPECT_EQ(val, 8000);
}

TEST(TransformTouchCoords, YAxisScale) {
  // Device range [0, 900] → VIRTUAL_MULTITOUCH_ABS_Y_MAX (9000)
  auto dev = make_device(0, 1000, 0, 900);
  int val = 450;
  TestRunnerRecorderTestPeer::transform(dev, val, Y_AXIS);
  // 450 * (9000 / 900) = 4500
  EXPECT_EQ(val, 4500);
}

TEST(TransformTouchCoords, XAxisMinimumNormalized) {
  // Device range [100, 600] — minimum is subtracted before scaling
  auto dev = make_device(100, 600, 0, 1000);
  int val = 350;  // effective 350 - 100 = 250 out of 500
  TestRunnerRecorderTestPeer::transform(dev, val, X_AXIS);
  // 250 * (16000 / 500) = 8000
  EXPECT_EQ(val, 8000);
}

TEST(TransformTouchCoords, YAxisMinimumNormalized) {
  auto dev = make_device(0, 1000, 200, 700);
  int val = 450;  // effective 450 - 200 = 250 out of 500
  TestRunnerRecorderTestPeer::transform(dev, val, Y_AXIS);
  // 250 * (9000 / 500) = 4500
  EXPECT_EQ(val, 4500);
}

TEST(TransformTouchCoords, XAxisZeroRange) {
  // When native_range == 0, transform_val stays 0, result should be 0
  auto dev = make_device(5, 5, 0, 1000);  // max == min → range 0
  int val = 5;
  TestRunnerRecorderTestPeer::transform(dev, val, X_AXIS);
  // val - minimum = 0, then * 0 = 0
  EXPECT_EQ(val, 0);
}

TEST(TransformTouchCoords, YAxisZeroRange) {
  auto dev = make_device(0, 1000, 3, 3);
  int val = 3;
  TestRunnerRecorderTestPeer::transform(dev, val, Y_AXIS);
  EXPECT_EQ(val, 0);
}

TEST(TransformTouchCoords, XAxisFullRange) {
  auto dev = make_device(0, 16000, 0, 9000);
  int val = 16000;
  TestRunnerRecorderTestPeer::transform(dev, val, X_AXIS);
  EXPECT_EQ(val, 16000);
}

TEST(TransformTouchCoords, XAxisZeroValue) {
  auto dev = make_device(0, 1000, 0, 1000);
  int val = 0;
  TestRunnerRecorderTestPeer::transform(dev, val, X_AXIS);
  EXPECT_EQ(val, 0);
}

// ── current_time_str ──────────────────────────────────────────────────────────

// Build a recorder for pure-logic calls (no syscalls needed since we only call
// current_time_str which reads no state beyond the argument).
static TestRunnerRecorder* make_pure_recorder(MockSyscall& mock) {
  EXPECT_CALL(mock, opendir(_)).WillRepeatedly(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir)).WillRepeatedly(ReturnNull());
  EXPECT_CALL(mock, closedir(kFakeDir)).WillRepeatedly(Return(0));
  return new TestRunnerRecorder((char*)"pure_test", 30, false, "/fake/input", &mock);
}

TEST(CurrentTimeStr, FormatsZeroAsAllZeros) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_pure_recorder(mock));
  TestRunnerRecorderTestPeer peer(*rec);
  EXPECT_EQ(peer.current_time_str(std::chrono::milliseconds(0)), "00:00:00.000");
}

TEST(CurrentTimeStr, FormatsOneSecond) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_pure_recorder(mock));
  TestRunnerRecorderTestPeer peer(*rec);
  EXPECT_EQ(peer.current_time_str(std::chrono::milliseconds(1000)), "00:00:01.000");
}

TEST(CurrentTimeStr, FormatsOneMinute) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_pure_recorder(mock));
  TestRunnerRecorderTestPeer peer(*rec);
  EXPECT_EQ(peer.current_time_str(std::chrono::milliseconds(60000)), "00:01:00.000");
}

TEST(CurrentTimeStr, FormatsMilliseconds) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_pure_recorder(mock));
  TestRunnerRecorderTestPeer peer(*rec);
  // 1h 2m 3s 456ms
  auto ms = std::chrono::milliseconds(
      1 * 3600000LL + 2 * 60000LL + 3 * 1000LL + 456LL);
  EXPECT_EQ(peer.current_time_str(ms), "01:02:03.456");
}

TEST(CurrentTimeStr, HoursWrapAt24) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_pure_recorder(mock));
  TestRunnerRecorderTestPeer peer(*rec);
  // 25 hours should wrap to 01:00:00.000
  auto ms = std::chrono::milliseconds(25LL * 3600000LL);
  EXPECT_EQ(peer.current_time_str(ms), "01:00:00.000");
}

TEST(CurrentTimeStr, PadsHoursMinutesSeconds) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_pure_recorder(mock));
  TestRunnerRecorderTestPeer peer(*rec);
  // 1h 1m 1s 1ms — all fields need zero-padding
  auto ms = std::chrono::milliseconds(
      1 * 3600000LL + 1 * 60000LL + 1 * 1000LL + 1LL);
  EXPECT_EQ(peer.current_time_str(ms), "01:01:01.001");
}

// ── setDevicesToRecord ────────────────────────────────────────────────────────

// Build a recorder with a known set of available devices (injected via
// findDevices through the mock), then call setDevicesToRecord.
//
// For these tests we reuse the keyboard device fixture from ClosesFdsOnDestruction.

static in_device_s make_in_device(int fd, const char* name, device_type type) {
  in_device_s d{};
  d.fd = fd;
  strncpy(d.name, name, sizeof(d.name) - 1);
  d.type = type;
  return d;
}

// Build a recorder with 3 fake available_devices by having findDevices discover
// three keyboard-type event files.
static TestRunnerRecorder* make_three_device_recorder(MockSyscall& mock,
                                               int fd0, int fd1, int fd2) {
  static struct dirent e0 = make_dirent("event0", DT_CHR);
  static struct dirent e1 = make_dirent("event1", DT_CHR);
  static struct dirent e2 = make_dirent("event2", DT_CHR);
  static struct dirent* seq[] = {&e0, &e1, &e2, nullptr};
  static int idx = 0;
  idx = 0;

  EXPECT_CALL(mock, opendir(_)).WillOnce(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir))
      .WillRepeatedly(Invoke([](DIR*) -> struct dirent* {
        return seq[idx < 4 ? idx++ : 3];
      }));
  EXPECT_CALL(mock, closedir(kFakeDir)).WillOnce(Return(0));

  // Three opens — return the supplied fds.
  EXPECT_CALL(mock, open(_, _))
      .WillOnce(Return(fd0))
      .WillOnce(Return(fd1))
      .WillOnce(Return(fd2));

  // EVIOCGNAME for each fd.
  auto fill_name = [](int, unsigned long, void* arg) {
    strncpy(static_cast<char*>(arg), "kbd", 50);
  };
  EXPECT_CALL(mock, ioctl(_, EVIOCGNAME(50), _))
      .Times(3)
      .WillRepeatedly(DoAll(Invoke(fill_name), Return(3)));

  // Capabilities: only EV_KEY.
  uint8_t caps[150]{};
  caps[EV_KEY / 8] = static_cast<uint8_t>(1 << (EV_KEY % 8));
  auto fill_caps = [caps](int, unsigned long, void* arg) {
    memcpy(arg, caps, 150);
  };
  EXPECT_CALL(mock, ioctl(_, EVIOCGBIT(0, 150), _))
      .Times(3)
      .WillRepeatedly(DoAll(Invoke(fill_caps), Return(150)));

  EXPECT_CALL(mock, ioctl(_, EVIOCGBIT(EV_ABS, 150), _))
      .Times(3)
      .WillRepeatedly(DoAll(
          Invoke([](int, unsigned long, void* arg) { memset(arg, 0, 150); }),
          Return(150)));

  EXPECT_CALL(mock, close(_)).WillRepeatedly(Return(0));

  return new TestRunnerRecorder((char*)"mask_test", 30, false, "/fake/input", &mock);
}

TEST(SetDevicesToRecord, SingleDevice) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_three_device_recorder(mock, 10, 11, 12));
  TestRunnerRecorderTestPeer peer(*rec);

  auto devs = rec->listDevices();
  ASSERT_EQ(devs.size(), 3u);

  // Record only device 1 (fd=11, index 1 → bit 1).
  rec->setDevicesToRecord({devs[1]});
  EXPECT_EQ(peer.get_device_mask(), 0b010ULL);
}

TEST(SetDevicesToRecord, MultipleDevices) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_three_device_recorder(mock, 10, 11, 12));
  TestRunnerRecorderTestPeer peer(*rec);

  auto devs = rec->listDevices();
  ASSERT_EQ(devs.size(), 3u);

  rec->setDevicesToRecord({devs[0], devs[2]});
  EXPECT_EQ(peer.get_device_mask(), 0b101ULL);
}

TEST(SetDevicesToRecord, EmptyListClearsMask) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_three_device_recorder(mock, 10, 11, 12));
  TestRunnerRecorderTestPeer peer(*rec);

  rec->setDevicesToRecord({});
  EXPECT_EQ(peer.get_device_mask(), 0ULL);
}

TEST(SetDevicesToRecord, DeviceNotInAvailableList) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_three_device_recorder(mock, 10, 11, 12));
  TestRunnerRecorderTestPeer peer(*rec);

  // Pass a device with fd=99, which was never opened.
  in_device_s unknown = make_in_device(99, "unknown", KEYBOARD);
  rec->setDevicesToRecord({unknown});
  EXPECT_EQ(peer.get_device_mask(), 0ULL);
}

TEST(SetDevicesToRecord, AllDevices) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_three_device_recorder(mock, 10, 11, 12));
  TestRunnerRecorderTestPeer peer(*rec);

  auto devs = rec->listDevices();
  rec->setDevicesToRecord(devs);
  EXPECT_EQ(peer.get_device_mask(), 0b111ULL);
}

// ── is_done ───────────────────────────────────────────────────────────────────

TEST(IsDone, StopFlagSetReturnsTrue) {
  MockSyscall mock;
  EXPECT_CALL(mock, opendir(_)).WillRepeatedly(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir)).WillRepeatedly(ReturnNull());
  EXPECT_CALL(mock, closedir(kFakeDir)).WillRepeatedly(Return(0));
  TestRunnerRecorder rec((char*)"done_test", 30, false, "/fake/input", &mock);
  TestRunnerRecorderTestPeer peer(rec);

  // m_stop is true by default (before Start).
  EXPECT_TRUE(peer.is_done());
}

TEST(IsDone, StopFlagClearedReturnsFalse) {
  MockSyscall mock;
  EXPECT_CALL(mock, opendir(_)).WillRepeatedly(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir)).WillRepeatedly(ReturnNull());
  EXPECT_CALL(mock, closedir(kFakeDir)).WillRepeatedly(Return(0));
  TestRunnerRecorder rec((char*)"done_test2", 30, false, "/fake/input", &mock);
  TestRunnerRecorderTestPeer peer(rec);

  peer.set_m_stop(false);
  // Not continuous, but recording length is 30s and start time is 0 (epoch).
  // current_time would be much larger, so time-based stop would fire.
  // Set start_time to now so the duration hasn't elapsed.
  peer.set_m_start_time(peer.current_time_ms());
  EXPECT_FALSE(peer.is_done());
}

TEST(IsDone, NonContinuousElapsedReturnsTrue) {
  MockSyscall mock;
  EXPECT_CALL(mock, opendir(_)).WillRepeatedly(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir)).WillRepeatedly(ReturnNull());
  EXPECT_CALL(mock, closedir(kFakeDir)).WillRepeatedly(Return(0));
  TestRunnerRecorder rec((char*)"elapsed_test", 30, false, "/fake/input", &mock);
  TestRunnerRecorderTestPeer peer(rec);

  peer.set_m_stop(false);
  peer.set_m_continuous(false);
  // Set start_time far in the past so duration > recording_length.
  peer.set_m_start_time(std::chrono::milliseconds(0));
  peer.set_m_recording_length(1000);  // 1 second — epoch is well past this

  EXPECT_TRUE(peer.is_done());
}

TEST(IsDone, ContinuousModeIgnoresTimeLimit) {
  MockSyscall mock;
  EXPECT_CALL(mock, opendir(_)).WillRepeatedly(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir)).WillRepeatedly(ReturnNull());
  EXPECT_CALL(mock, closedir(kFakeDir)).WillRepeatedly(Return(0));
  TestRunnerRecorder rec((char*)"cont_elapsed", 30, true, "/fake/input", &mock);
  TestRunnerRecorderTestPeer peer(rec);

  peer.set_m_stop(false);
  peer.set_m_continuous(true);
  // Set start_time far in past — continuous mode should NOT stop on time.
  peer.set_m_start_time(std::chrono::milliseconds(0));
  peer.set_m_recording_length(1000);

  EXPECT_FALSE(peer.is_done());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 3 — Syscall-mocked tests
// ═══════════════════════════════════════════════════════════════════════════════

// ── helpers ───────────────────────────────────────────────────────────────────

static void set_cap(uint8_t* buf, int bit) {
  buf[bit / 8] |= static_cast<uint8_t>(1 << (bit % 8));
}

// ── findDevices device-type detection ────────────────────────────────────────

// Sets up one event device that reports the given capabilities.
// GMock matches in LIFO order, so we register the broad catch-all first and
// the specific matchers last so the specific ones take priority.
static TestRunnerRecorder* make_one_device_recorder(MockSyscall& mock,
                                             int fd,
                                             uint8_t* caps,
                                             uint8_t* abs_caps,
                                             input_absinfo abs_x,
                                             input_absinfo abs_y,
                                             const char* name = "dev") {
  static struct dirent e0 = make_dirent("event0", DT_CHR);
  static struct dirent* seq[] = {&e0, nullptr};
  static int idx = 0;
  idx = 0;

  EXPECT_CALL(mock, opendir(_)).WillOnce(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir))
      .WillRepeatedly(Invoke([](DIR*) -> struct dirent* {
        return seq[idx < 2 ? idx++ : 1];
      }));
  EXPECT_CALL(mock, closedir(kFakeDir)).WillOnce(Return(0));
  EXPECT_CALL(mock, close(_)).WillRepeatedly(Return(0));
  EXPECT_CALL(mock, open(_, _)).WillOnce(Return(fd));

  // Catch-all for EVIOCGABS (registered FIRST so specific matchers below
  // override it — GMock uses LIFO priority for overlapping matchers).
  // EVIOCGABS can't be used with testing::_ because it's an arithmetic macro.
  input_absinfo ax = abs_x;
  input_absinfo ay = abs_y;
  std::atomic<int> abs_call{0};
  EXPECT_CALL(mock, ioctl(fd, _, _))
      .WillRepeatedly(DoAll(
          Invoke([ax, ay, &abs_call](int, unsigned long, void* arg) mutable {
            if (abs_call.fetch_add(1) % 2 == 0) {
              memcpy(arg, &ax, sizeof(input_absinfo));
            } else {
              memcpy(arg, &ay, sizeof(input_absinfo));
            }
          }),
          Return(0)));

  // Specific matchers — registered after catch-all so they match first (LIFO).
  uint8_t abs_copy[150];
  memcpy(abs_copy, abs_caps, 150);
  EXPECT_CALL(mock, ioctl(fd, EVIOCGBIT(EV_ABS, 150), _))
      .WillOnce(DoAll(
          Invoke([abs_copy](int, unsigned long, void* arg) { memcpy(arg, abs_copy, 150); }),
          Return(150)));

  uint8_t caps_copy[150];
  memcpy(caps_copy, caps, 150);
  EXPECT_CALL(mock, ioctl(fd, EVIOCGBIT(0, 150), _))
      .WillOnce(DoAll(
          Invoke([caps_copy](int, unsigned long, void* arg) { memcpy(arg, caps_copy, 150); }),
          Return(150)));

  EXPECT_CALL(mock, ioctl(fd, EVIOCGNAME(50), _))
      .WillOnce(DoAll(
          Invoke([name](int, unsigned long, void* arg) {
            strncpy(static_cast<char*>(arg), name, 50);
          }),
          Return(static_cast<int>(strlen(name)))));

  return new TestRunnerRecorder((char*)"finddev_test", 30, false, "/fake/input", &mock);
}

TEST(FindDevices, DiscoversMouse) {
  MockSyscall mock;
  uint8_t caps[150]{};
  uint8_t abs_caps[150]{};
  set_cap(caps, EV_REL);
  input_absinfo zero{};

  std::unique_ptr<TestRunnerRecorder> rec(
      make_one_device_recorder(mock, 5, caps, abs_caps, zero, zero, "mouse0"));
  auto devs = rec->listDevices();
  ASSERT_EQ(devs.size(), 1u);
  EXPECT_EQ(devs[0].type, MOUSE);
}

TEST(FindDevices, DiscoversKeyboard) {
  MockSyscall mock;
  uint8_t caps[150]{};
  uint8_t abs_caps[150]{};
  set_cap(caps, EV_KEY);
  input_absinfo zero{};

  std::unique_ptr<TestRunnerRecorder> rec(
      make_one_device_recorder(mock, 6, caps, abs_caps, zero, zero, "kbd0"));
  auto devs = rec->listDevices();
  ASSERT_EQ(devs.size(), 1u);
  EXPECT_EQ(devs[0].type, KEYBOARD);
}

TEST(FindDevices, DiscoversTouchscreen) {
  MockSyscall mock;
  uint8_t caps[150]{};
  uint8_t abs_caps[150]{};
  set_cap(caps, EV_ABS);
  // has_abs_x and has_abs_y checked from device_capabilities, not abs_capabilities
  set_cap(caps, ABS_X);
  set_cap(caps, ABS_Y);
  input_absinfo abs_x{};
  abs_x.minimum = 0;
  abs_x.maximum = 1920;
  input_absinfo abs_y{};
  abs_y.minimum = 0;
  abs_y.maximum = 1080;

  std::unique_ptr<TestRunnerRecorder> rec(
      make_one_device_recorder(mock, 7, caps, abs_caps, abs_x, abs_y, "ts0"));
  auto devs = rec->listDevices();
  ASSERT_EQ(devs.size(), 1u);
  EXPECT_EQ(devs[0].type, TOUCHSCREEN);
  EXPECT_EQ(devs[0].abs_range_x.maximum, 1920);
}

TEST(FindDevices, DiscoversMultiTouchscreen) {
  MockSyscall mock;
  uint8_t caps[150]{};
  uint8_t abs_caps[150]{};
  set_cap(caps, EV_ABS);
  set_cap(abs_caps, ABS_MT_SLOT);
  set_cap(abs_caps, ABS_MT_TRACKING_ID);
  input_absinfo abs_x{};
  abs_x.minimum = 0;
  abs_x.maximum = 4096;
  input_absinfo abs_y{};
  abs_y.minimum = 0;
  abs_y.maximum = 2048;

  std::unique_ptr<TestRunnerRecorder> rec(
      make_one_device_recorder(mock, 8, caps, abs_caps, abs_x, abs_y, "mts0"));
  auto devs = rec->listDevices();
  ASSERT_EQ(devs.size(), 1u);
  EXPECT_EQ(devs[0].type, MULTITOUCHSCREEN);
  EXPECT_EQ(devs[0].abs_range_x.maximum, 4096);
}

TEST(FindDevices, SkipsDeviceOnOpenFailure) {
  MockSyscall mock;

  static struct dirent e0 = make_dirent("event0", DT_CHR);
  static struct dirent* seq[] = {&e0, nullptr};
  static int idx = 0;
  idx = 0;

  EXPECT_CALL(mock, opendir(_)).WillOnce(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir))
      .WillRepeatedly(Invoke([](DIR*) -> struct dirent* {
        return seq[idx < 2 ? idx++ : 1];
      }));
  EXPECT_CALL(mock, closedir(kFakeDir)).WillOnce(Return(0));
  EXPECT_CALL(mock, open(_, _)).WillOnce(Return(-1));

  TestRunnerRecorder rec((char*)"open_fail", 30, false, "/fake/input", &mock);
  EXPECT_TRUE(rec.listDevices().empty());
}

TEST(FindDevices, IoctlNameFailureContinues) {
  MockSyscall mock;
  uint8_t caps[150]{};
  uint8_t abs_caps[150]{};
  set_cap(caps, EV_KEY);

  static struct dirent e0 = make_dirent("event0", DT_CHR);
  static struct dirent* seq[] = {&e0, nullptr};
  static int idx = 0;
  idx = 0;

  EXPECT_CALL(mock, opendir(_)).WillOnce(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir))
      .WillRepeatedly(Invoke([](DIR*) -> struct dirent* {
        return seq[idx < 2 ? idx++ : 1];
      }));
  EXPECT_CALL(mock, closedir(kFakeDir)).WillOnce(Return(0));
  EXPECT_CALL(mock, open(_, _)).WillOnce(Return(9));

  // EVIOCGNAME fails → device still enumerated with empty name.
  EXPECT_CALL(mock, ioctl(9, EVIOCGNAME(50), _)).WillOnce(Return(-1));
  EXPECT_CALL(mock, ioctl(9, EVIOCGBIT(0, 150), _))
      .WillOnce(DoAll(
          Invoke([caps](int, unsigned long, void* arg) { memcpy(arg, caps, 150); }),
          Return(150)));
  EXPECT_CALL(mock, ioctl(9, EVIOCGBIT(EV_ABS, 150), _))
      .WillOnce(DoAll(
          Invoke([abs_caps](int, unsigned long, void* arg) { memcpy(arg, abs_caps, 150); }),
          Return(150)));
  EXPECT_CALL(mock, close(9)).WillOnce(Return(0));

  TestRunnerRecorder rec((char*)"name_fail", 30, false, "/fake/input", &mock);
  ASSERT_EQ(rec.listDevices().size(), 1u);
  EXPECT_EQ(rec.listDevices()[0].type, KEYBOARD);
}

TEST(FindDevices, SkipsUnknownDeviceType) {
  MockSyscall mock;
  // No EV_KEY, EV_REL, or EV_ABS bits set → device is unknown → skipped.
  uint8_t caps[150]{};
  uint8_t abs_caps[150]{};

  static struct dirent e0 = make_dirent("event0", DT_CHR);
  static struct dirent* seq[] = {&e0, nullptr};
  static int idx = 0;
  idx = 0;

  EXPECT_CALL(mock, opendir(_)).WillOnce(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir))
      .WillRepeatedly(Invoke([](DIR*) -> struct dirent* {
        return seq[idx < 2 ? idx++ : 1];
      }));
  EXPECT_CALL(mock, closedir(kFakeDir)).WillOnce(Return(0));
  EXPECT_CALL(mock, open(_, _)).WillOnce(Return(10));

  EXPECT_CALL(mock, ioctl(10, EVIOCGNAME(50), _))
      .WillOnce(DoAll(
          Invoke([](int, unsigned long, void* arg) {
            strncpy(static_cast<char*>(arg), "unknown", 50);
          }),
          Return(7)));
  EXPECT_CALL(mock, ioctl(10, EVIOCGBIT(0, 150), _))
      .WillOnce(DoAll(
          Invoke([caps](int, unsigned long, void* arg) { memcpy(arg, caps, 150); }),
          Return(150)));
  EXPECT_CALL(mock, ioctl(10, EVIOCGBIT(EV_ABS, 150), _))
      .WillOnce(DoAll(
          Invoke([abs_caps](int, unsigned long, void* arg) { memcpy(arg, abs_caps, 150); }),
          Return(150)));
  // Skipped devices are not added to available_devices and their fd is not
  // closed during findDevices (production behavior — fd leak for unknown types).
  EXPECT_CALL(mock, close(_)).WillRepeatedly(Return(0));

  TestRunnerRecorder rec((char*)"unknown_type", 30, false, "/fake/input", &mock);
  EXPECT_TRUE(rec.listDevices().empty());
}

// ── Start / Stop / isActive ──────────────────────────────────────────────────

// Returns a recorder with a keyboard device where select() always returns 0
// (timeout), so Record() loops harmlessly until stopped.
static TestRunnerRecorder* make_startable_recorder(MockSyscall& mock, int fd = 20) {
  static struct dirent e0 = make_dirent("event0", DT_CHR);
  static struct dirent* seq[] = {&e0, nullptr};
  static int idx = 0;
  idx = 0;

  EXPECT_CALL(mock, opendir(_)).WillOnce(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir))
      .WillRepeatedly(Invoke([](DIR*) -> struct dirent* {
        return seq[idx < 2 ? idx++ : 1];
      }));
  EXPECT_CALL(mock, closedir(kFakeDir)).WillOnce(Return(0));
  EXPECT_CALL(mock, open(_, _)).WillOnce(Return(fd));

  uint8_t caps[150]{};
  set_cap(caps, EV_KEY);
  uint8_t abs_caps[150]{};

  EXPECT_CALL(mock, ioctl(fd, EVIOCGNAME(50), _))
      .WillOnce(DoAll(
          Invoke([](int, unsigned long, void* arg) {
            strncpy(static_cast<char*>(arg), "kb", 50);
          }),
          Return(2)));
  EXPECT_CALL(mock, ioctl(fd, EVIOCGBIT(0, 150), _))
      .WillOnce(DoAll(
          Invoke([caps](int, unsigned long, void* arg) { memcpy(arg, caps, 150); }),
          Return(150)));
  EXPECT_CALL(mock, ioctl(fd, EVIOCGBIT(EV_ABS, 150), _))
      .WillOnce(DoAll(
          Invoke([abs_caps](int, unsigned long, void* arg) { memcpy(arg, abs_caps, 150); }),
          Return(150)));

  EXPECT_CALL(mock, select(_, _, _, _, _)).WillRepeatedly(Return(0));
  EXPECT_CALL(mock, close(_)).WillRepeatedly(Return(0));

  return new TestRunnerRecorder((char*)"start_test", 2, false, "/fake/input", &mock);
}

TEST(StartStopActive, NotActiveBeforeStart) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_startable_recorder(mock));
  EXPECT_FALSE(rec->isActive());
}

TEST(StartStopActive, ActiveAfterStart) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_startable_recorder(mock));
  rec->Start();
  // m_stop is cleared by the spawned thread, not by Start() itself — spin-wait.
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!rec->isActive() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  EXPECT_TRUE(rec->isActive());
  rec->Stop();
}

TEST(StartStopActive, NotActiveAfterStop) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_startable_recorder(mock));
  rec->Start();
  // Wait for thread to become active before stopping, else Stop() races with
  // the thread that sets m_stop=false after Start() returns.
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!rec->isActive() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  rec->Stop();
  EXPECT_FALSE(rec->isActive());
}

TEST(StartStopActive, SecondStartNoOp) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_startable_recorder(mock));
  rec->Start();
  // Wait for the recorder to become active before calling Start() again.
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!rec->isActive() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  // Second Start() while already active must not crash or spawn a second thread.
  rec->Start();
  EXPECT_TRUE(rec->isActive());
  rec->Stop();
}

TEST(StartStopActive, StopReturnsFilePath) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_startable_recorder(mock));
  rec->Start();
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!rec->isActive() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  std::string path = rec->Stop();
  EXPECT_EQ(path, "/tmp/start_test.rrc");
}

// ── Record — event processing ────────────────────────────────────────────────

// Drive Record() with one KEY_A event then an ESC to stop the loop.
// Verifies the event is written to the non-continuous output file.
TEST(Record, NonContinuousWritesEventToFile) {
  MockSyscall mock;

  static struct dirent e0 = make_dirent("event0", DT_CHR);
  static struct dirent* seq[] = {&e0, nullptr};
  static int dir_idx = 0;
  dir_idx = 0;

  EXPECT_CALL(mock, opendir(_)).WillOnce(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir))
      .WillRepeatedly(Invoke([](DIR*) -> struct dirent* {
        return seq[dir_idx < 2 ? dir_idx++ : 1];
      }));
  EXPECT_CALL(mock, closedir(kFakeDir)).WillOnce(Return(0));

  const int kFd = 30;
  EXPECT_CALL(mock, open(_, _)).WillOnce(Return(kFd));

  uint8_t caps[150]{};
  uint8_t abs_caps[150]{};
  set_cap(caps, EV_KEY);

  EXPECT_CALL(mock, ioctl(kFd, EVIOCGNAME(50), _))
      .WillOnce(DoAll(
          Invoke([](int, unsigned long, void* arg) {
            strncpy(static_cast<char*>(arg), "kb", 50);
          }),
          Return(2)));
  EXPECT_CALL(mock, ioctl(kFd, EVIOCGBIT(0, 150), _))
      .WillOnce(DoAll(
          Invoke([caps](int, unsigned long, void* arg) { memcpy(arg, caps, 150); }),
          Return(150)));
  EXPECT_CALL(mock, ioctl(kFd, EVIOCGBIT(EV_ABS, 150), _))
      .WillOnce(DoAll(
          Invoke([abs_caps](int, unsigned long, void* arg) { memcpy(arg, abs_caps, 150); }),
          Return(150)));

  struct input_event esc_ev{};
  esc_ev.type = EV_KEY;
  esc_ev.code = KEY_ESC;
  esc_ev.value = 1;

  struct input_event key_a{};
  key_a.type = EV_KEY;
  key_a.code = KEY_A;
  key_a.value = 1;

  // select returns 1 for the first two calls (key_a then ESC), then 0.
  std::atomic<int> select_calls{0};
  EXPECT_CALL(mock, select(_, _, _, _, _))
      .WillRepeatedly(Invoke([&](int, fd_set* rfds, fd_set*, fd_set*, timeval*) -> int {
        int call = select_calls.fetch_add(1);
        if (call == 0 || call == 1) {
          FD_SET(kFd, rfds);
          return 1;
        }
        return 0;
      }));

  std::atomic<int> read_calls{0};
  EXPECT_CALL(mock, read(kFd, _, sizeof(input_event)))
      .WillRepeatedly(Invoke([&](int, void* buf, size_t sz) -> ssize_t {
        int call = read_calls.fetch_add(1);
        if (call == 0) {
          memcpy(buf, &key_a, sz);
        } else {
          memcpy(buf, &esc_ev, sz);
        }
        return static_cast<ssize_t>(sz);
      }));

  EXPECT_CALL(mock, close(_)).WillRepeatedly(Return(0));

  const char* test_file = "/tmp/rec_write_test.rrc";
  std::remove(test_file);

  TestRunnerRecorder rec((char*)"rec_write_test", 30, false, "/fake/input", &mock);
  rec.Start();
  // Phase 1: wait for thread to become active (m_stop cleared by thread).
  {
    auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!rec.isActive() && std::chrono::steady_clock::now() < dl) {
      std::this_thread::yield();
    }
  }
  // Phase 2: let the ESC event stop the recording from within the thread.
  // isActive() becomes false after the thread processes ESC and exits.
  {
    auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (rec.isActive() && std::chrono::steady_clock::now() < dl) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  rec.Stop();

  std::ifstream f(test_file);
  ASSERT_TRUE(f.good()) << "Recording file not found: " << test_file;
  std::string content((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
  EXPECT_NE(content.find("passThrough"), std::string::npos);
}

TEST(Record, SelectErrorBreaksLoop) {
  MockSyscall mock;

  static struct dirent e0 = make_dirent("event0", DT_CHR);
  static struct dirent* seq[] = {&e0, nullptr};
  static int dir_idx = 0;
  dir_idx = 0;

  EXPECT_CALL(mock, opendir(_)).WillOnce(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir))
      .WillRepeatedly(Invoke([](DIR*) -> struct dirent* {
        return seq[dir_idx < 2 ? dir_idx++ : 1];
      }));
  EXPECT_CALL(mock, closedir(kFakeDir)).WillOnce(Return(0));

  const int kFd = 31;
  EXPECT_CALL(mock, open(_, _)).WillOnce(Return(kFd));

  uint8_t caps[150]{};
  uint8_t abs_caps[150]{};
  set_cap(caps, EV_KEY);

  EXPECT_CALL(mock, ioctl(kFd, EVIOCGNAME(50), _))
      .WillOnce(DoAll(
          Invoke([](int, unsigned long, void* arg) {
            strncpy(static_cast<char*>(arg), "kb", 50);
          }),
          Return(2)));
  EXPECT_CALL(mock, ioctl(kFd, EVIOCGBIT(0, 150), _))
      .WillOnce(DoAll(
          Invoke([caps](int, unsigned long, void* arg) { memcpy(arg, caps, 150); }),
          Return(150)));
  EXPECT_CALL(mock, ioctl(kFd, EVIOCGBIT(EV_ABS, 150), _))
      .WillOnce(DoAll(
          Invoke([abs_caps](int, unsigned long, void* arg) { memcpy(arg, abs_caps, 150); }),
          Return(150)));

  // select() returns -1 → Record() should break the loop immediately.
  EXPECT_CALL(mock, select(_, _, _, _, _)).WillRepeatedly(Return(-1));
  EXPECT_CALL(mock, close(_)).WillRepeatedly(Return(0));

  TestRunnerRecorder rec((char*)"select_err_test", 30, false, "/fake/input", &mock);
  rec.Start();
  // Wait for thread to start (sets m_stop=false), then let select() -1 break the loop.
  {
    auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!rec.isActive() && std::chrono::steady_clock::now() < dl) {
      std::this_thread::yield();
    }
  }
  // Give the thread a moment to process select()=-1 and exit, then stop cleanly.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  rec.Stop();
  EXPECT_FALSE(rec.isActive());
}

TEST(Record, ReadEagainIgnored) {
  MockSyscall mock;

  static struct dirent e0 = make_dirent("event0", DT_CHR);
  static struct dirent* seq[] = {&e0, nullptr};
  static int dir_idx = 0;
  dir_idx = 0;

  EXPECT_CALL(mock, opendir(_)).WillOnce(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir))
      .WillRepeatedly(Invoke([](DIR*) -> struct dirent* {
        return seq[dir_idx < 2 ? dir_idx++ : 1];
      }));
  EXPECT_CALL(mock, closedir(kFakeDir)).WillOnce(Return(0));

  const int kFd = 32;
  EXPECT_CALL(mock, open(_, _)).WillOnce(Return(kFd));

  uint8_t caps[150]{};
  uint8_t abs_caps[150]{};
  set_cap(caps, EV_KEY);

  EXPECT_CALL(mock, ioctl(kFd, EVIOCGNAME(50), _))
      .WillOnce(DoAll(
          Invoke([](int, unsigned long, void* arg) {
            strncpy(static_cast<char*>(arg), "kb", 50);
          }),
          Return(2)));
  EXPECT_CALL(mock, ioctl(kFd, EVIOCGBIT(0, 150), _))
      .WillOnce(DoAll(
          Invoke([caps](int, unsigned long, void* arg) { memcpy(arg, caps, 150); }),
          Return(150)));
  EXPECT_CALL(mock, ioctl(kFd, EVIOCGBIT(EV_ABS, 150), _))
      .WillOnce(DoAll(
          Invoke([abs_caps](int, unsigned long, void* arg) { memcpy(arg, abs_caps, 150); }),
          Return(150)));

  struct input_event esc_ev{};
  esc_ev.type = EV_KEY;
  esc_ev.code = KEY_ESC;
  esc_ev.value = 1;

  std::atomic<int> select_calls{0};
  EXPECT_CALL(mock, select(_, _, _, _, _))
      .WillRepeatedly(Invoke([&](int, fd_set* rfds, fd_set*, fd_set*, timeval*) -> int {
        int call = select_calls.fetch_add(1);
        if (call == 0 || call == 1) {
          FD_SET(kFd, rfds);
          return 1;
        }
        return 0;
      }));

  std::atomic<int> read_calls{0};
  EXPECT_CALL(mock, read(kFd, _, sizeof(input_event)))
      .WillRepeatedly(Invoke([&](int, void* buf, size_t sz) -> ssize_t {
        int call = read_calls.fetch_add(1);
        if (call == 0) {
          errno = EAGAIN;
          return -1;
        }
        memcpy(buf, &esc_ev, sz);
        return static_cast<ssize_t>(sz);
      }));

  EXPECT_CALL(mock, close(_)).WillRepeatedly(Return(0));

  TestRunnerRecorder rec((char*)"eagain_test", 30, false, "/fake/input", &mock);
  rec.Start();
  {
    auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!rec.isActive() && std::chrono::steady_clock::now() < dl) {
      std::this_thread::yield();
    }
  }
  rec.Stop();
  EXPECT_FALSE(rec.isActive());
}

// ── Constructor filename paths ────────────────────────────────────────────────

TEST(ConstructorFilename, NonContinuousPath) {
  MockSyscall mock;
  expect_empty_dir(mock);
  TestRunnerRecorder rec((char*)"myfile", 30, false, "/fake/input", &mock);
  rec.Start();
  {
    auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!rec.isActive() && std::chrono::steady_clock::now() < dl) {
      std::this_thread::yield();
    }
  }
  std::string path = rec.Stop();
  EXPECT_EQ(path, "/tmp/myfile.rrc");
}

TEST(ConstructorFilename, ContinuousPathHasTimestamp) {
  MockSyscall mock;
  EXPECT_CALL(mock, opendir(_)).WillRepeatedly(Return(kFakeDir));
  EXPECT_CALL(mock, readdir(kFakeDir)).WillRepeatedly(ReturnNull());
  EXPECT_CALL(mock, closedir(kFakeDir)).WillRepeatedly(Return(0));

  TestRunnerRecorder rec((char*)"snap", 2, true, "/fake/input", &mock);
  rec.Start();
  {
    auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!rec.isActive() && std::chrono::steady_clock::now() < dl) {
      std::this_thread::yield();
    }
  }
  std::string path = rec.Stop();
  std::regex ts_re(R"(/tmp/snap_\d{8}_\d{6}\.rrc)");
  EXPECT_TRUE(std::regex_match(path, ts_re)) << "Path was: " << path;
}

// ── Destructor with active recorder ──────────────────────────────────────────

TEST(Destructor, JoinsActiveThread) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_startable_recorder(mock, 50));
  rec->Start();
  // Spin-wait so the thread is actually running before we destroy.
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!rec->isActive() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  rec.reset();  // ~TestRunnerRecorder() must join thread cleanly.
}

TEST(Destructor, NoJoinWhenNeverStarted) {
  MockSyscall mock;
  std::unique_ptr<TestRunnerRecorder> rec(make_startable_recorder(mock, 51));
  rec.reset();  // ~TestRunnerRecorder() should not call join() on unjoinable thread.
}
