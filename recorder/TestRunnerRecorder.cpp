// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include "TestRunnerRecorder.h"

#include <filesystem>
#include <iomanip>

#include <xkbcommon/xkbcommon.h>

#include "SyscallInterface.h"
#include "spdlog/spdlog.h"

static int open_restricted(const char* path, int flags, void* user_data) {
  int fd = open(path, flags);
  return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void* user_data) {
  close(fd);
}

#if ENABLE_LIBINPUT
const static struct libinput_interface interface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};
#endif

TestRunnerRecorder::TestRunnerRecorder(char* filename,
                         uint32_t length,
                         bool continuous,
                         std::string input_directory,
                         SyscallInterface* syscalls)
    : input_dir(std::move(input_directory)),
      m_recording_length(length * 1000),
      m_continuous(continuous),
      m_device_mask(0xFFFFFFFFFFFFFFFF),
      m_stop(true),
      m_continuous_limit_reached(false),
      m_start_time(0),
      m_stop_time(0) {
  if (syscalls != nullptr) {
    m_syscalls = syscalls;
  } else {
    m_owned_syscalls = std::make_unique<RealSyscalls>();
    m_syscalls = m_owned_syscalls.get();
  }
  findDevices();
  setup_xkb();
  if (!continuous) {
    std::string filename_str = filename;
    m_filename = "/tmp/" + std::filesystem::path(filename_str).filename().string() + ".rrc";
    m_recording_path = m_filename;
  } else {
    /* Continuous recording will append a timestamp when completed. */
    m_filename = filename;
  }
}

TestRunnerRecorder::~TestRunnerRecorder() {
  if (m_xkb_state)   xkb_state_unref(m_xkb_state);
  if (m_xkb_keymap)  xkb_keymap_unref(m_xkb_keymap);
  if (m_xkb_ctx)     xkb_context_unref(m_xkb_ctx);

#if ENABLE_LIBINPUT
  // Restore Accel Profiles
  spdlog::info("Restoring Mouse Acceleration profiles");
  for (device_bu_s device_bu : device_backups) {
    libinput_device_config_accel_set_profile(device_bu.dev, device_bu.profile);
  }
  libinput_unref(li);
#endif
  if (m_recorder_thread.joinable()) {
    {
      std::lock_guard<std::mutex> lock(stop_mtx);
      m_stop = true;
      m_stop_time = current_time_ms();
    }
    stop_cv.notify_all();
    m_recorder_thread.join();
  }

  // Close the file descriptors
  for (in_device_s in_device : available_devices) {
    m_syscalls->close(in_device.fd);
  }
}

void TestRunnerRecorder::setup_xkb() {
  m_xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!m_xkb_ctx) {
    spdlog::warn("Failed to create XKB context — keysyms will not be recorded");
    return;
  }
  // Use env vars (XKB_DEFAULT_LAYOUT etc.) or system default if unset.
  xkb_rule_names rules{};
  m_xkb_keymap = xkb_keymap_new_from_names(m_xkb_ctx, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!m_xkb_keymap) {
    spdlog::warn("XKB keymap compilation failed — keysyms will not be recorded");
    xkb_context_unref(m_xkb_ctx);
    m_xkb_ctx = nullptr;
    return;
  }
  m_xkb_state = xkb_state_new(m_xkb_keymap);
  if (!m_xkb_state) {
    spdlog::warn("Failed to create XKB state — keysyms will not be recorded");
    xkb_keymap_unref(m_xkb_keymap);
    xkb_context_unref(m_xkb_ctx);
    m_xkb_keymap = nullptr;
    m_xkb_ctx = nullptr;
  }
}

void TestRunnerRecorder::findDevices() {
  int result = 0;
  DIR* dir = nullptr;
  struct dirent* ent = nullptr;
  if ((dir = m_syscalls->opendir(input_dir.c_str())) != nullptr) {
    // Read all entries in the directory
    while ((ent = m_syscalls->readdir(dir)) != nullptr) {
      // Check if the entry is a device file
      if (ent->d_type == DT_CHR) {
        if (strncmp(ent->d_name, "event", 5) == 0) {
          std::string dev_path = input_dir + "/" + ent->d_name;
          int fd = m_syscalls->open(dev_path.c_str(), O_RDONLY | O_NONBLOCK);
          if (fd != -1) {
            char name[50];
            char device_capabilities[150];
            char abs_capabilities[150];
            in_device_s indev{};
            indev.fd = fd;
            std::string type_str;

            result = m_syscalls->ioctl(fd, EVIOCGNAME(50), &name);
            if (result < 0) {
              spdlog::warn("Couldn't retrieve device name");
            }
            strncpy(indev.name, name, 50);
            indev.name[sizeof(indev.name) - 1] = '\0';

            spdlog::info("Monitoring: {}", dev_path);
            spdlog::info("\tName: {}", indev.name);

            result =
                m_syscalls->ioctl(fd, EVIOCGBIT(0, 150), device_capabilities);
            if (result < 0) {
              spdlog::warn("Couldn't retrieve device capabilities");
            }

            result = m_syscalls->ioctl(
                fd, EVIOCGBIT(EV_ABS, sizeof(abs_capabilities)),
                abs_capabilities);
            if (result < 0) {
              spdlog::warn("Couldn't retrieve device abs capabilities");
            }

            if ((device_capabilities[EV_ABS / 8] & (1 << (EV_ABS % 8))) != 0) {
              bool has_mt_slot =
                  ((abs_capabilities[ABS_MT_SLOT / 8] >> (ABS_MT_SLOT % 8)) &
                   1) != 0;
              bool has_mt_tracking_id =
                  ((abs_capabilities[ABS_MT_TRACKING_ID / 8] >>
                    (ABS_MT_TRACKING_ID % 8)) &
                   1) != 0;
              bool has_abs_x =
                  ((device_capabilities[ABS_X / 8] >> (ABS_X % 8)) & 1) != 0;
              bool has_abs_y =
                  ((device_capabilities[ABS_Y / 8] >> (ABS_Y % 8)) & 1) != 0;

              if (has_mt_slot && has_mt_tracking_id) {
                indev.type = MULTITOUCHSCREEN;
                type_str = "Multi-Touchscreen";
                result = m_syscalls->ioctl(indev.fd, EVIOCGABS(ABS_MT_POSITION_X),
                                           &indev.abs_range_x);
                if (result < 0) {
                  spdlog::warn("Couldn't retrieve device abs range x");
                }
                result = m_syscalls->ioctl(indev.fd, EVIOCGABS(ABS_MT_POSITION_Y),
                                           &indev.abs_range_y);
                if (result < 0) {
                  spdlog::warn("Couldn't retrieve device abs range y");
                }
              } else if (has_abs_x && has_abs_y) {
                indev.type = TOUCHSCREEN;
                type_str = "Touchscreen";

                result = m_syscalls->ioctl(indev.fd, EVIOCGABS(ABS_X),
                                           &indev.abs_range_x);
                if (result < 0) {
                  spdlog::warn("Couldn't retrieve device abs range x");
                }

                result = m_syscalls->ioctl(indev.fd, EVIOCGABS(ABS_Y),
                                           &indev.abs_range_y);
                if (result < 0) {
                  spdlog::warn("Couldn't retrieve device abs range y");
                }
              } else {
                spdlog::info("Skipping device at {}: '{}'.", dev_path, name);
                continue;
              }

            } else if ((device_capabilities[EV_REL / 8] &
                        (1 << (EV_REL % 8))) != 0) {
              indev.type = MOUSE;
              type_str = "Mouse";

              result = m_syscalls->ioctl(indev.fd, EVIOCGABS(ABS_X),
                                         &indev.abs_range_x);
            } else if ((device_capabilities[EV_KEY / 8] &
                        (1 << (EV_KEY % 8))) != 0) {
              indev.type = KEYBOARD;
              type_str = "Keyboard";
            } else {
              spdlog::info("Skipping device at {}: '{}'.", dev_path,
                           indev.name);
              continue;
            }
            spdlog::info("\tType: {}", type_str);
            spdlog::info("\tfd: {}", indev.fd);
            if (indev.type == MULTITOUCHSCREEN) {
              spdlog::info("\tABS_MT_POSITION_X Min: {} Max: {}",
                           indev.abs_range_x.minimum,
                           indev.abs_range_x.maximum);
              spdlog::info("\tABS_MT_POSITION_Y Min: {} Max: {}",
                           indev.abs_range_y.minimum,
                           indev.abs_range_y.maximum);
            } else if (indev.type == TOUCHSCREEN) {
              spdlog::info("\tABS_X Min: {} Max: {}", indev.abs_range_x.minimum,
                           indev.abs_range_x.maximum);
              spdlog::info("\tABS_Y Min: {} Max: {}", indev.abs_range_y.minimum,
                           indev.abs_range_y.maximum);
            }
            available_devices.push_back(indev);
          } else {
            std::cerr << "Error opening " << dev_path << ": " << strerror(errno)
                      << std::endl;
          }
        }
      }
    }
#if ENABLE_LIBINPUT
    struct udev* udev = udev_new();
    enum libinput_config_accel_profile profile;

    li = libinput_udev_create_context(&interface, nullptr, udev);
    libinput_udev_assign_seat(li, "seat0");
    libinput_dispatch(li);

    while ((ev = libinput_get_event(li))) {
      if (libinput_event_get_type(ev) == LIBINPUT_EVENT_DEVICE_ADDED) {
        struct libinput_device* dev = libinput_event_get_device(ev);
        const char* name = libinput_device_get_name(dev);

        if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER)) {
          if (libinput_device_config_accel_is_available(dev)) {
            profile = libinput_device_config_accel_get_profile(dev);

            // Record profile/speed, set them back after recorder is complete
            device_backups.push_back({dev, profile});

            // set profile to flat
            spdlog::info("Setting {} Acceleration profile to Flat", name);
            libinput_device_config_accel_set_profile(
                dev, LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
          }
        }
        libinput_event_destroy(ev);
        libinput_dispatch(li);
      }
    }
#endif
    m_syscalls->closedir(dir);
  } else {
    std::cerr << "Error opening directory " << input_dir << ": "
              << strerror(errno) << std::endl;
  }
}

std::vector<in_device_s> TestRunnerRecorder::listDevices() {
  return available_devices;
}

void TestRunnerRecorder::setDevicesToRecord(const std::vector<in_device_s>& devices) {
  m_device_mask = 0;
  for (int i = 0; i < available_devices.size(); i++) {
    bool record_device = false;
    for (in_device_s device : devices) {
      if (available_devices[i].fd == device.fd) {
        record_device = true;
      }
    }
    if (record_device) {
      m_device_mask |= (1ULL << i);
    }
  }
}

void TestRunnerRecorder::Start() {
  if (!isActive()) {
    m_recorder_thread = std::thread(&TestRunnerRecorder::Record, this);
  }
}

void TestRunnerRecorder::Record() {
  std::ofstream cmd_file;
  int max_fd = 0;
  struct timeval timeout = {};
  timeout.tv_sec = 0;
  timeout.tv_usec = 10 * 1000;  // 10ms
  std::vector<in_device_s> devices_to_record;
  fd_set read_fds;
  std::thread bufferThread;
  {
    std::lock_guard<std::mutex> lock(stop_mtx);
    m_stop = false;
    m_start_time = current_time_ms();
  }

  if (m_continuous) {
    bufferThread = std::thread(&TestRunnerRecorder::processBuffer, this);
  }
  // Monitor the file descriptors using select
  spdlog::debug("Device Mask: {0:#x}", m_device_mask);
  for (int i = 0; i < available_devices.size(); i++) {
    if ((m_device_mask & (1ULL << i)) != 0) {
      devices_to_record.push_back(available_devices[i]);
      if (available_devices[i].fd > max_fd) {
        max_fd = available_devices[i].fd;
      }
      spdlog::debug("Recording device: {}", available_devices[i].name);
    }
  }

  if (!m_continuous) {
    cmd_file.open(m_filename);
    std::string start_time_str = current_time_str(m_start_time);
    cmd_file << "version " << RECORDING_FORMAT_VERSION << "\n";
    cmd_file << "[" << start_time_str << "]"
             << "\n";
  }

  spdlog::info("Beginning Recording: Length: {}  Continuous: {}",
               m_recording_length, m_continuous);
  while (!is_done()) {
    FD_ZERO(&read_fds);
    for (in_device_s in_device : devices_to_record) {
      FD_SET(in_device.fd, &read_fds);
      if (in_device.fd > max_fd) {
        max_fd = in_device.fd;
      }
    }

    int num_ready = m_syscalls->select(max_fd + 1, &read_fds, nullptr, nullptr,
                                       &timeout);
    if (num_ready == 0) {
      continue;
    }
    if (num_ready == -1) {
      spdlog::error("Error in select: {}", strerror(errno));
      break;
    }
    if (num_ready > 0) {
      for (in_device_s in_device : devices_to_record) {
        if (FD_ISSET(in_device.fd, &read_fds)) {
          struct input_event ev {};
          ssize_t bytes_read =
              m_syscalls->read(in_device.fd, &ev, sizeof(ev));
          if (bytes_read > 0) {
            auto ts_abs = current_time_ms();

            /* If this is a touch, transform the coordinates to our arbitrary
            ranges, so when the recording is played back, the touches will be in
            the correct spots, even if played back on a different screen size.
          */
            if (ev.type == EV_ABS) {
              if ((ev.code == ABS_MT_POSITION_X) || (ev.code == ABS_X)) {
                transform_touch_coordinates(in_device, ev.value, X_AXIS);
              } else if ((ev.code == ABS_MT_POSITION_Y) || (ev.code == ABS_Y)) {
                transform_touch_coordinates(in_device, ev.value, Y_AXIS);
              } else {
                // Nothing
              }
            }

            if (ev.type == EV_KEY && ev.code == KEY_ESC) {
              std::lock_guard<std::mutex> lock(stop_mtx);
              spdlog::debug("Received ESC key");
              m_stop = true;
              m_stop_time = current_time_ms();
              continue;
            }

            // Resolve keysym for keyboard key events before updating XKB state.
            uint32_t keysym = 0;
            if (ev.type == EV_KEY && in_device.type == KEYBOARD && m_xkb_state) {
              auto xkb_code = static_cast<xkb_keycode_t>(ev.code + 8);
              keysym = xkb_state_key_get_one_sym(m_xkb_state, xkb_code);
              if (ev.value == 1) {
                xkb_state_update_key(m_xkb_state, xkb_code, XKB_KEY_DOWN);
              } else if (ev.value == 0) {
                xkb_state_update_key(m_xkb_state, xkb_code, XKB_KEY_UP);
              }
            }

            /* If this is not a continuous recording, just immediately write
             * each input to the file. */
            if (!m_continuous) {
              std::string time_str = current_time_str(ts_abs);
              cmd_file << "[" << time_str << "] "
                       << "passThrough " << in_device.type << " " << ev.type
                       << " " << ev.code << " " << ev.value;
              if (keysym) {
                cmd_file << " " << keysym;
              }
              cmd_file << "\n";
            }
            /* If it is continous, keep the input in local memory until we
               decide to dump it to a file all at once. */
            else {
              std::lock_guard<std::mutex> lock(buf_mtx);
              m_input_buf.push_back(
                  {ts_abs, in_device.type, ev.type, ev.code, ev.value, keysym});
            }
          } else if (bytes_read == -1 && errno != EAGAIN &&
                     errno != EWOULDBLOCK) {
            spdlog::error("Error reading from fd {}: {}", in_device.fd,
                          strerror(errno));
          }
        }
      }
    }
  }
  if (m_continuous) {
    spdlog::info("Recording completed or stopped, waiting for file write...");
    bufferThread.join();
  }
  spdlog::info("Recording Complete");
}

std::string TestRunnerRecorder::Stop() {
  {
    std::lock_guard<std::mutex> lock(stop_mtx);
    m_stop = true;
    m_stop_time = current_time_ms();
  }
  stop_cv.notify_all();
  spdlog::debug("Received Stop Command");

  m_recorder_thread.join();

  return m_recording_path;
}

bool TestRunnerRecorder::isActive() {
  std::lock_guard<std::mutex> lock(stop_mtx);
  return !m_stop;
}

void TestRunnerRecorder::processBuffer() {
  while (!is_done()) {
    {
      std::unique_lock<std::mutex> lk(stop_mtx);
      stop_cv.wait_for(lk, std::chrono::seconds(1), [this] { return m_stop.load(); });
    }
    auto current_time = current_time_ms();
    std::lock_guard<std::mutex> lock(buf_mtx);
    auto cutoff_time = current_time.count() - m_recording_length;
    while (!m_input_buf.empty() &&
           ((m_input_buf[0].ts_abs.count() < cutoff_time) ||
            (m_input_buf.size() > FILE_MAX_LINES))) {
      m_input_buf.pop_front();
    }
  }
  /* Final trim */
  auto rec_start_time = m_stop_time.count() - m_recording_length;
  std::lock_guard<std::mutex> lock(buf_mtx);
  while (!m_input_buf.empty() &&
         (m_input_buf[0].ts_abs.count() < rec_start_time)) {
    spdlog::debug("popping record: rec_ts: {}, rec_start_time: {} buf_size: {}",
                  m_input_buf[0].ts_abs.count(), rec_start_time,
                  m_input_buf.size());
    m_input_buf.pop_front();
  }

  /* Dump to file. */
  spdlog::debug("Dumping recording to file...");
  std::ofstream cmd_file;
  std::string file_ts = file_timestamp();
  m_recording_path = "/tmp/" + m_filename + "_" + file_ts + ".rrc";
  cmd_file.open(m_recording_path);
  std::string start_time_str;
  if ((m_stop_time.count() - m_start_time.count()) >= m_recording_length) {
    std::chrono::milliseconds rec_start_dur(rec_start_time);
    start_time_str = current_time_str(rec_start_dur);
  } else {
    start_time_str = current_time_str(m_start_time);
  }
  cmd_file << "version " << RECORDING_FORMAT_VERSION << "\n";
  cmd_file << "[" << start_time_str << "]"
           << "\n";
  for (const input_record_s& record : m_input_buf) {
    std::string time_str = current_time_str(record.ts_abs);
    cmd_file << "[" << time_str << "] "
             << "passThrough " << record.dev_type << " " << record.input_type
             << " " << record.input_code << " " << record.input_val;
    if (record.keysym) {
      cmd_file << " " << record.keysym;
    }
    cmd_file << "\n";
  }
}

void TestRunnerRecorder::transform_touch_coordinates(in_device_s device,
                                              int& val,
                                              int axis) {
  double native_range = 0.0;
  double transform_val = 0.0;

  if (axis == X_AXIS) {
    // Normalize to 0 minimum just in case
    val = val - device.abs_range_x.minimum;
    native_range = device.abs_range_x.maximum - device.abs_range_x.minimum;
    if (native_range > 0) {
      transform_val = (double)VIRTUAL_MULTITOUCH_ABS_X_MAX / native_range;
    }
  } else {
    // Normalize to 0 minimum just in case
    val = val - device.abs_range_y.minimum;
    native_range = device.abs_range_y.maximum - device.abs_range_y.minimum;
    if (native_range > 0) {
      transform_val = (double)VIRTUAL_MULTITOUCH_ABS_Y_MAX / native_range;
    }
  }

  val = int(val * transform_val);
}

bool TestRunnerRecorder::is_done() {
  // Fast path: check the atomic without locking.
  if (m_stop.load(std::memory_order_acquire)) {
    return true;
  }
  // Slow path: check time-based stop (only for non-continuous mode).
  if (!m_continuous) {
    auto current_time = current_time_ms();
    std::lock_guard<std::mutex> lock(stop_mtx);
    if ((current_time.count() - m_start_time.count()) >= m_recording_length) {
      m_stop = true;
      stop_cv.notify_all();
      spdlog::debug("Recording reached end of length");
      return true;
    }
  }
  return false;
}

std::chrono::milliseconds TestRunnerRecorder::current_time_ms() {
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration);
}

std::string TestRunnerRecorder::current_time_str(std::chrono::milliseconds ms) {
  auto hours = std::chrono::duration_cast<std::chrono::hours>(ms);
  hours %= std::chrono::hours(24);
  ms %= std::chrono::hours(1);

  auto minutes = std::chrono::duration_cast<std::chrono::minutes>(ms);
  ms %= std::chrono::minutes(1);

  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(ms);
  ms %= std::chrono::seconds(1);

  std::ostringstream oss;
  oss << std::setfill('0') << std::setw(2) << hours.count() << ":"
      << std::setw(2) << minutes.count() << ":" << std::setw(2)
      << seconds.count() << "." << std::setw(3) << ms.count();

  return oss.str();
}

std::string TestRunnerRecorder::file_timestamp() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_struct = *std::localtime(&t);

  std::ostringstream oss;
  oss << std::put_time(&tm_struct, "%Y%m%d_%H%M%S");

  return oss.str();
}
