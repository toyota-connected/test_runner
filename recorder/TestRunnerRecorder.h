// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <linux/input.h>
#include <sys/select.h>
#include <unistd.h>
#include <atomic>

#include <xkbcommon/xkbcommon.h>

#define FILE_MAX_LINES (500000)

enum device_type {
  MOUSE = 0,
  KEYBOARD,
  TOUCHSCREEN,
  MULTITOUCHSCREEN,
  UNKNOWN
};

enum { X_AXIS = 0, Y_AXIS };

struct in_device_s {
  int fd;
  char name[50];
  device_type type;
  input_absinfo abs_range_x;
  input_absinfo abs_range_y;
};

#if ENABLE_LIBINPUT
#include <libinput.h>
struct device_bu_s {
  libinput_device* dev;
  enum libinput_config_accel_profile profile;
};
#endif

static constexpr uint32_t RECORDING_FORMAT_VERSION = 2;

struct input_record_s {
  std::chrono::milliseconds ts_abs;
  device_type dev_type;
  int input_type;
  int input_code;
  int input_val;
  uint32_t
      keysym;  // XKB keysym for EV_KEY events on keyboard devices; 0 otherwise
};

// Forward declaration — defined in test/mocks/SyscallInterface.h (tests) or
// linked against RealSyscalls in production.
struct SyscallInterface;
struct RealSyscalls;

// Forward declaration of test peer — defined in test/unit only.
class TestRunnerRecorderTestPeer;

class TestRunnerRecorder {
 public:
  TestRunnerRecorder(char* filename,
                     uint32_t length,
                     bool continuous,
                     std::string input_directory = "/dev/input",
                     SyscallInterface* syscalls = nullptr);
  ~TestRunnerRecorder();

  std::vector<in_device_s> listDevices();
  void setDevicesToRecord(const std::vector<in_device_s>& devices);
  void Start();
  std::string Stop();
  bool isActive();

 private:
  std::vector<in_device_s> available_devices;
  std::deque<input_record_s> m_input_buf;
  std::string input_dir;
  std::unique_ptr<RealSyscalls> m_owned_syscalls;
  SyscallInterface* m_syscalls;

  xkb_context* m_xkb_ctx = nullptr;
  xkb_keymap* m_xkb_keymap = nullptr;
  xkb_state* m_xkb_state = nullptr;
  std::string m_filename;
  std::string m_recording_path;
  std::mutex stop_mtx;
  std::condition_variable stop_cv;
  std::mutex buf_mtx;
  std::atomic<bool> m_stop = false;
  uint32_t m_recording_length;
  bool m_continuous;
  bool m_continuous_limit_reached;
  std::chrono::milliseconds m_start_time;
  std::chrono::milliseconds m_stop_time;
  std::thread m_recorder_thread;
  uint64_t m_device_mask;

#if ENABLE_LIBINPUT
  std::vector<device_bu_s> device_backups;
  struct libinput* li;
  struct libinput_event* ev;
#endif

  friend class TestRunnerRecorderTestPeer;

  void Record();
  void findDevices();
  void processBuffer();
  void setup_xkb();
  static void transform_touch_coordinates(in_device_s device,
                                          int& val,
                                          int axis);
  bool is_done();
  static std::chrono::milliseconds current_time_ms();
  static std::string current_time_str(std::chrono::milliseconds ms);
  static std::string file_timestamp();
};