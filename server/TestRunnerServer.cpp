// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include "TestRunnerServer.h"

#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <iostream>

#include <linux/uinput.h>
#include <sys/ioctl.h>

#include <xkbcommon/xkbcommon.h>

#include "SyscallInterface.h"

#include <capnp/ez-rpc.h>
#include <capnp/message.h>
#include <capnp/serialize.h>

#include "capnp/test_runner.capnp.h"
#include "capnp/test_runner_server.capnp.h"

#include "spdlog/spdlog.h"

static ErrorHandlerImpl errorHandler;
static kj::TaskSet taskSet_(errorHandler);

TestRunnerServer::TestRunnerServer(SyscallInterface* syscalls,
                                   bool enable_plugins,
                                   bool enable_snapshot_recorder,
                                   const XkbConfig& xkb_cfg)
    : mDeviceFd{-1, -1, -1, -1} {
  if (syscalls != nullptr) {
    m_syscalls = syscalls;
  } else {
    m_owned_syscalls = std::make_unique<RealSyscalls>();
    m_syscalls = m_owned_syscalls.get();
  }
  spdlog::set_level(spdlog::level::debug);

  setup_virtual_mouse();
  setup_virtual_keyboard();
  setup_virtual_touchscreen();
  setup_virtual_multi_touchscreen();
  setup_xkb(xkb_cfg);

  if (enable_plugins) {
    init_plugins();
  }

#if ENABLE_RECORDER
  if (enable_snapshot_recorder) {
    snapshot_recorder = std::make_unique<TestRunnerRecorder>(
        (char*)"input_snapshot", 30, true, "/dev/input", m_syscalls);
    snapshot_recorder->Start();
  }
#else
  (void)enable_snapshot_recorder;
#endif
}

TestRunnerServer::~TestRunnerServer() {
#if ENABLE_RECORDER
  // Stop recorders first: their threads call m_syscalls (virtual dispatch)
  // which requires m_owned_syscalls to still be alive, and they must not be
  // running when close_input_device() destroys the uinput devices they read.
  snapshot_recorder.reset();
  custom_recorders.clear();
#endif

  for (const int i : mDeviceFd) {
    close_input_device(i);
  }

  if (m_xkb_keymap)
    xkb_keymap_unref(m_xkb_keymap);
  if (m_xkb_ctx)
    xkb_context_unref(m_xkb_ctx);
}

void TestRunnerServer::setup_virtual_keyboard() {
  int result = 0;

  mDeviceFd[VIRTUAL_KEYBOARD] = m_syscalls->open("/dev/uinput", O_WRONLY);
  if (mDeviceFd[VIRTUAL_KEYBOARD] >= 0) {
    uinput_capability_setup(mDeviceFd[VIRTUAL_KEYBOARD], UI_SET_EVBIT, EV_KEY);
    for (uint16_t key = KEY_ESC; key < KEY_MAX; key++) {
      result = m_syscalls->ioctl_int(mDeviceFd[VIRTUAL_KEYBOARD], UI_SET_KEYBIT,
                                     key);
      if (result < 0) {
        spdlog::error("Failed to init key: {}", key);
      }
    }

    uinput_setup ui_setup{};
    strcpy(ui_setup.name, "test_runner_keyboard");
    ui_setup.id.bustype = BUS_VIRTUAL;
    ui_setup.id.vendor = 0x23;
    ui_setup.id.product = 0x47;
    ui_setup.id.version = 1;

    result =
        m_syscalls->ioctl(mDeviceFd[VIRTUAL_KEYBOARD], UI_DEV_SETUP, &ui_setup);
    if (result < 0) {
      spdlog::error("Failed to setup virtual keyboard: {}", errno);
    }

    result =
        m_syscalls->ioctl(mDeviceFd[VIRTUAL_KEYBOARD], UI_DEV_CREATE, nullptr);
    if (result < 0) {
      spdlog::error("Failed to create virtual keyboard: {}", errno);
      m_syscalls->ioctl(mDeviceFd[VIRTUAL_KEYBOARD], UI_DEV_DESTROY, nullptr);
    }

    m_syscalls->usleep(250000);
    spdlog::info("Ready for keyboard inputs");
  } else {
    spdlog::error("Failed to open /dev/uinput");
  }
}

void TestRunnerServer::setup_xkb(const XkbConfig& cfg) {
  m_xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!m_xkb_ctx) {
    spdlog::error("Failed to create XKB context");
    return;
  }

  auto env_or = [](const std::string& val, const char* env) -> const char* {
    if (!val.empty())
      return val.c_str();
    return getenv(env);
  };

  xkb_rule_names rules{};
  rules.rules = env_or(cfg.rules, "XKB_DEFAULT_RULES");
  rules.model = env_or(cfg.model, "XKB_DEFAULT_MODEL");
  rules.layout = env_or(cfg.layout, "XKB_DEFAULT_LAYOUT");
  rules.variant = env_or(cfg.variant, "XKB_DEFAULT_VARIANT");
  rules.options = env_or(cfg.options, "XKB_DEFAULT_OPTIONS");

  m_xkb_keymap =
      xkb_keymap_new_from_names(m_xkb_ctx, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!m_xkb_keymap) {
    spdlog::error(
        "XKB keymap compilation failed — keySym/keyName injection disabled");
    xkb_context_unref(m_xkb_ctx);
    m_xkb_ctx = nullptr;
    return;
  }
  spdlog::info("XKB keymap loaded: layout='{}' variant='{}' model='{}'",
               rules.layout ? rules.layout : "",
               rules.variant ? rules.variant : "",
               rules.model ? rules.model : "");
}

uint32_t TestRunnerServer::resolve_keysym(xkb_keysym_t target_sym) const {
  if (!m_xkb_keymap)
    return 0;
  // Search unshifted level first, then shifted, to prefer the simpler mapping.
  for (uint32_t level = 0; level <= 1; level++) {
    for (uint32_t evdev = KEY_ESC; evdev < KEY_MAX; evdev++) {
      auto xkb_code = static_cast<xkb_keycode_t>(evdev + 8);
      uint32_t num_layouts =
          xkb_keymap_num_layouts_for_key(m_xkb_keymap, xkb_code);
      for (uint32_t layout_idx = 0; layout_idx < num_layouts; layout_idx++) {
        const xkb_keysym_t* syms = nullptr;
        int n = xkb_keymap_key_get_syms_by_level(m_xkb_keymap, xkb_code,
                                                 layout_idx, level, &syms);
        for (int i = 0; i < n; i++) {
          if (syms[i] == target_sym)
            return evdev;
        }
      }
    }
  }
  return 0;
}

void TestRunnerServer::setup_virtual_touchscreen() {
  int result = 0;
  constexpr int keys_enabled[1] = {BTN_TOUCH};
  constexpr int abs_move_enabled[2] = {ABS_X, ABS_Y};

  mDeviceFd[VIRTUAL_TOUCHSCREEN] = m_syscalls->open("/dev/uinput", O_WRONLY);
  if (mDeviceFd[VIRTUAL_TOUCHSCREEN] >= 0) {
    uinput_capability_setup(mDeviceFd[VIRTUAL_TOUCHSCREEN], UI_SET_EVBIT,
                            EV_KEY);
    for (const int i : keys_enabled) {
      uinput_capability_setup(mDeviceFd[VIRTUAL_TOUCHSCREEN], UI_SET_KEYBIT, i);
    }

    uinput_capability_setup(mDeviceFd[VIRTUAL_TOUCHSCREEN], UI_SET_EVBIT,
                            EV_ABS);
    for (const int i : abs_move_enabled) {
      uinput_capability_setup(mDeviceFd[VIRTUAL_TOUCHSCREEN], UI_SET_ABSBIT, i);
    }

    uinput_setup ui_setup{};
    strcpy(ui_setup.name, "test_runner_touchscreen");
    ui_setup.id.bustype = BUS_VIRTUAL;
    ui_setup.id.vendor = 0x23;
    ui_setup.id.product = 0x46;
    ui_setup.id.version = 1;

    result = m_syscalls->ioctl_int(mDeviceFd[VIRTUAL_TOUCHSCREEN],
                                   UI_SET_PROPBIT, INPUT_PROP_DIRECT);
    if (result < 0) {
      spdlog::error("Failed to set propbit: {}", errno);
    }

    result = m_syscalls->ioctl(mDeviceFd[VIRTUAL_TOUCHSCREEN], UI_DEV_SETUP,
                               &ui_setup);
    if (result < 0) {
      spdlog::error("Failed to setup virtual touchscreen: {}", errno);
    }

    static constexpr uinput_abs_setup abs_x_setup = {
        .code = ABS_X,
        .absinfo = {.minimum = 0, .maximum = VIRTUAL_MULTITOUCH_ABS_X_MAX}};
    result = m_syscalls->ioctl(mDeviceFd[VIRTUAL_TOUCHSCREEN], UI_ABS_SETUP,
                               const_cast<uinput_abs_setup*>(&abs_x_setup));
    if (result < 0) {
      spdlog::error("Failed to setup ABS_X: {}", errno);
    }
    static constexpr uinput_abs_setup abs_y_setup = {
        .code = ABS_Y,
        .absinfo = {.minimum = 0, .maximum = VIRTUAL_MULTITOUCH_ABS_Y_MAX}};
    result = m_syscalls->ioctl(mDeviceFd[VIRTUAL_TOUCHSCREEN], UI_ABS_SETUP,
                               const_cast<uinput_abs_setup*>(&abs_y_setup));
    if (result < 0) {
      spdlog::error("Failed to setup ABS_Y: {}", errno);
    }

    result = m_syscalls->ioctl(mDeviceFd[VIRTUAL_TOUCHSCREEN], UI_DEV_CREATE,
                               nullptr);
    if (result < 0) {
      spdlog::error("Failed to create virtual touchscreen: {}", errno);
      m_syscalls->ioctl(mDeviceFd[VIRTUAL_TOUCHSCREEN], UI_DEV_DESTROY,
                        nullptr);
    }

    m_syscalls->usleep(250000);
    spdlog::info("Ready for touch inputs");
  } else {
    spdlog::error("Failed to open /dev/uinput");
  }
}

void TestRunnerServer::setup_virtual_multi_touchscreen() {
  int result = 0;
  constexpr int keys_enabled[1] = {BTN_TOUCH};
  constexpr int abs_move_enabled[4] = {ABS_MT_SLOT, ABS_MT_TRACKING_ID,
                                       ABS_MT_POSITION_X, ABS_MT_POSITION_Y};

  mDeviceFd[VIRTUAL_MULTI_TOUCHSCREEN] =
      m_syscalls->open("/dev/uinput", O_WRONLY);
  if (mDeviceFd[VIRTUAL_MULTI_TOUCHSCREEN] >= 0) {
    uinput_capability_setup(mDeviceFd[VIRTUAL_MULTI_TOUCHSCREEN], UI_SET_EVBIT,
                            EV_KEY);
    for (int i : keys_enabled) {
      uinput_capability_setup(mDeviceFd[VIRTUAL_MULTI_TOUCHSCREEN],
                              UI_SET_KEYBIT, i);
    }

    uinput_capability_setup(mDeviceFd[VIRTUAL_MULTI_TOUCHSCREEN], UI_SET_EVBIT,
                            EV_ABS);
    for (int i : abs_move_enabled) {
      uinput_capability_setup(mDeviceFd[VIRTUAL_MULTI_TOUCHSCREEN],
                              UI_SET_ABSBIT, i);
    }

    uinput_setup ui_setup{};
    strcpy(ui_setup.name, "test_runner_multi_touchscreen");
    ui_setup.id.bustype = BUS_VIRTUAL;
    ui_setup.id.vendor = 0x23;
    ui_setup.id.product = 0x46;
    ui_setup.id.version = 1;

    result = m_syscalls->ioctl_int(mDeviceFd[VIRTUAL_MULTI_TOUCHSCREEN],
                                   UI_SET_PROPBIT, INPUT_PROP_DIRECT);
    if (result < 0) {
      spdlog::error("Failed to set propbit: {}", errno);
    }

    result = m_syscalls->ioctl(mDeviceFd[VIRTUAL_MULTI_TOUCHSCREEN],
                               UI_DEV_SETUP, &ui_setup);
    if (result < 0) {
      spdlog::error("Failed to setup virtual multi-touchscreen: {}", errno);
    }

    static constexpr uinput_abs_setup abs_mt_x_setup = {
        .code = ABS_MT_POSITION_X,
        .absinfo = {.minimum = 0, .maximum = VIRTUAL_MULTITOUCH_ABS_X_MAX}};
    result =
        m_syscalls->ioctl(mDeviceFd[VIRTUAL_MULTI_TOUCHSCREEN], UI_ABS_SETUP,
                          const_cast<uinput_abs_setup*>(&abs_mt_x_setup));
    if (result < 0) {
      spdlog::error("Failed to setup ABS_MT_X: {}", errno);
    }
    static constexpr uinput_abs_setup abs_mt_y_setup = {
        .code = ABS_MT_POSITION_Y,
        .absinfo = {.minimum = 0, .maximum = VIRTUAL_MULTITOUCH_ABS_Y_MAX}};
    result =
        m_syscalls->ioctl(mDeviceFd[VIRTUAL_MULTI_TOUCHSCREEN], UI_ABS_SETUP,
                          const_cast<uinput_abs_setup*>(&abs_mt_y_setup));
    if (result < 0) {
      spdlog::error("Failed to setup ABS_MT_Y: {}", errno);
    }
    static constexpr uinput_abs_setup abs_mt_slot_setup = {
        .code = ABS_MT_SLOT, .absinfo = {.minimum = 0, .maximum = 4}};
    result =
        m_syscalls->ioctl(mDeviceFd[VIRTUAL_MULTI_TOUCHSCREEN], UI_ABS_SETUP,
                          const_cast<uinput_abs_setup*>(&abs_mt_slot_setup));
    if (result < 0) {
      spdlog::error("Failed to setup ABS_MT_SLOT: {}", errno);
    }

    result = m_syscalls->ioctl(mDeviceFd[VIRTUAL_MULTI_TOUCHSCREEN],
                               UI_DEV_CREATE, nullptr);
    if (result < 0) {
      spdlog::error("Failed to create virtual multi-touchscreen: {}", errno);
      m_syscalls->ioctl(mDeviceFd[VIRTUAL_MULTI_TOUCHSCREEN], UI_DEV_DESTROY,
                        nullptr);
    }

    m_syscalls->usleep(250000);
    spdlog::info("Ready for multi-touch inputs");
  } else {
    spdlog::error("Failed to open /dev/uinput");
  }
}

void TestRunnerServer::setup_virtual_mouse() {
  int result = 0;
  constexpr int keys_enabled[2] = {BTN_LEFT, BTN_RIGHT};
  const int rel_move_enabled[5] = {REL_X, REL_Y, REL_Z, REL_WHEEL, REL_HWHEEL};

  mDeviceFd[VIRTUAL_MOUSE] = m_syscalls->open("/dev/uinput", O_WRONLY);
  if (mDeviceFd[VIRTUAL_MOUSE] >= 0) {
    uinput_capability_setup(mDeviceFd[VIRTUAL_MOUSE], UI_SET_EVBIT, EV_REL);
    for (const int i : rel_move_enabled) {
      uinput_capability_setup(mDeviceFd[VIRTUAL_MOUSE], UI_SET_RELBIT, i);
    }

    uinput_capability_setup(mDeviceFd[VIRTUAL_MOUSE], UI_SET_EVBIT, EV_KEY);
    for (const int i : keys_enabled) {
      uinput_capability_setup(mDeviceFd[VIRTUAL_MOUSE], UI_SET_KEYBIT, i);
    }

    uinput_setup ui_setup{};
    strcpy(ui_setup.name, "test_runner_mouse");
    ui_setup.id.bustype = BUS_VIRTUAL;
    ui_setup.id.vendor = 0x23;
    ui_setup.id.product = 0x45;
    ui_setup.id.version = 1;

    result =
        m_syscalls->ioctl(mDeviceFd[VIRTUAL_MOUSE], UI_DEV_SETUP, &ui_setup);
    if (result < 0) {
      spdlog::error("Failed to setup virtual mouse: {}", errno);
    }

    result =
        m_syscalls->ioctl(mDeviceFd[VIRTUAL_MOUSE], UI_DEV_CREATE, nullptr);
    if (result < 0) {
      spdlog::error("Failed to create virtual mouse: {}", errno);
      m_syscalls->ioctl(mDeviceFd[VIRTUAL_MOUSE], UI_DEV_DESTROY, nullptr);
    }

    m_syscalls->usleep(250000);
    spdlog::info("Ready for mouse inputs");
  } else {
    spdlog::error("Failed to open /dev/uinput");
  }
}

kj::Promise<void> TestRunnerServer::handleMouseMove(
    HandleMouseMoveContext context) {
  const auto mouseMove = context.getParams().getMouseMove();
  spdlog::debug("MouseMove received: relX={}, relY={}", mouseMove.getRelX(),
                mouseMove.getRelY());
  emit(getDeviceFd(VIRTUAL_MOUSE), EV_REL, REL_X, mouseMove.getRelX());
  emit(getDeviceFd(VIRTUAL_MOUSE), EV_REL, REL_Y, mouseMove.getRelY());
  emit(getDeviceFd(VIRTUAL_MOUSE), EV_SYN, SYN_REPORT, 0);
  context.getResults();
  return kj::READY_NOW;
}

kj::Promise<void> TestRunnerServer::handleMouseClick(
    HandleMouseClickContext context) {
  const auto mouseClick = context.getParams().getMouseClick();
  spdlog::debug("MouseClick received: btn={}, pressed={}", mouseClick.getBtn(),
                mouseClick.getPressed());
  emit(getDeviceFd(VIRTUAL_MOUSE), EV_KEY, mouseClick.getBtn(),
       mouseClick.getPressed());
  emit(getDeviceFd(VIRTUAL_MOUSE), EV_SYN, SYN_REPORT, 0);
  context.getResults();
  return kj::READY_NOW;
}

kj::Promise<void> TestRunnerServer::handleKeyPress(
    HandleKeyPressContext context) {
  const auto keyPress = context.getParams().getKeyPress();
  uint32_t evdev_code = 0;

  switch (keyPress.which()) {
    case KeyPress::RAW_CODE:
      evdev_code = keyPress.getRawCode();
      break;
    case KeyPress::KEY_SYM: {
      evdev_code =
          resolve_keysym(static_cast<xkb_keysym_t>(keyPress.getKeySym()));
      if (!evdev_code) {
        spdlog::warn("No keycode found for keysym 0x{:x}",
                     keyPress.getKeySym());
        context.getResults();
        return kj::READY_NOW;
      }
      break;
    }
    case KeyPress::KEY_NAME: {
      const char* name = keyPress.getKeyName().cStr();
      xkb_keysym_t sym = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS);
      if (sym == XKB_KEY_NoSymbol) {
        spdlog::warn("Unknown key name: '{}'", name);
        context.getResults();
        return kj::READY_NOW;
      }
      evdev_code = resolve_keysym(sym);
      if (!evdev_code) {
        spdlog::warn("No keycode found for key name '{}'", name);
        context.getResults();
        return kj::READY_NOW;
      }
      break;
    }
  }

  spdlog::debug("KeyPress received: code={}, pressed={}", evdev_code,
                keyPress.getPressed());
  emit(getDeviceFd(VIRTUAL_KEYBOARD), EV_KEY, static_cast<int>(evdev_code),
       keyPress.getPressed());
  emit(getDeviceFd(VIRTUAL_KEYBOARD), EV_SYN, SYN_REPORT, 0);
  context.getResults();
  return kj::READY_NOW;
}

kj::Promise<void> TestRunnerServer::handleSingleTouch(
    HandleSingleTouchContext context) {
  const auto singleTouch = context.getParams().getSingleTouch();
  spdlog::debug("SingleTouch received: absX={}, absY={}, pressed={}",
                singleTouch.getAbsX(), singleTouch.getAbsY(),
                singleTouch.getPressed());
  if (singleTouch.getAbsX() >= 0) {
    emit(getDeviceFd(VIRTUAL_TOUCHSCREEN), EV_ABS, ABS_X,
         singleTouch.getAbsX());
  }
  if (singleTouch.getAbsY() >= 0) {
    emit(getDeviceFd(VIRTUAL_TOUCHSCREEN), EV_ABS, ABS_Y,
         singleTouch.getAbsY());
  }
  emit(getDeviceFd(VIRTUAL_TOUCHSCREEN), EV_KEY, BTN_TOUCH,
       singleTouch.getPressed());
  emit(getDeviceFd(VIRTUAL_TOUCHSCREEN), EV_SYN, SYN_REPORT, 0);
  context.getResults();
  return kj::READY_NOW;
}

kj::Promise<void> TestRunnerServer::handleMultiTouch(
    HandleMultiTouchContext context) {
  const auto multiTouchList = context.getParams().getMultiTouch();
  spdlog::debug("MultiTouch received:");
  for (const auto& multiTouch : multiTouchList) {
    spdlog::debug("  Slot={}, TrackingId={}, AbsX={}, AbsY={}",
                  multiTouch.getSlot(), multiTouch.getTrackingId(),
                  multiTouch.getAbsX(), multiTouch.getAbsY());
    emit(getDeviceFd(VIRTUAL_MULTI_TOUCHSCREEN), EV_ABS, ABS_MT_SLOT,
         multiTouch.getSlot());
    if (multiTouch.getTrackingId() != 0) {
      multiTouch.getSlot() == -1 ? spdlog::debug("\tTouch released.")
                                 : spdlog::debug("\tNew touch with ID: {}",
                                                 multiTouch.getTrackingId());
      emit(getDeviceFd(VIRTUAL_MULTI_TOUCHSCREEN), EV_ABS, ABS_MT_TRACKING_ID,
           multiTouch.getTrackingId());
    }
    if (multiTouch.getTrackingId() >= 0) {
      spdlog::debug("\tTouch coordinates: X: {} Y: {}", multiTouch.getAbsX(),
                    multiTouch.getAbsY());
      emit(getDeviceFd(VIRTUAL_MULTI_TOUCHSCREEN), EV_ABS, ABS_MT_POSITION_X,
           multiTouch.getAbsX());
      emit(getDeviceFd(VIRTUAL_MULTI_TOUCHSCREEN), EV_ABS, ABS_MT_POSITION_Y,
           multiTouch.getAbsY());
    }
  }
  emit(getDeviceFd(VIRTUAL_MULTI_TOUCHSCREEN), EV_SYN, SYN_REPORT, 0);
  context.getResults();
  return kj::READY_NOW;
}

kj::Promise<void> TestRunnerServer::handlePassThrough(
    HandlePassThroughContext context) {
  const auto passThrough = context.getParams().getPassThrough();
  spdlog::debug("Passthrough Device:{} Type:{} Code:{} Value:{}",
                passThrough.getDevice(), passThrough.getType(),
                passThrough.getCode(), passThrough.getValue());
  emit(getDeviceFd(passThrough.getDevice()),
       static_cast<int>(passThrough.getType()),
       static_cast<int>(passThrough.getCode()), passThrough.getValue());
  context.getResults();
  return kj::READY_NOW;
}

void TestRunnerServer::uinput_capability_setup(int fd, int type, int val) {
  int result = m_syscalls->ioctl_int(fd, type, val);
  if (result < 0) {
    spdlog::error("uinput capability setup failed: Type: {} Value: {}", type,
                  val);
  }
}

void TestRunnerServer::emit(int fd, int type, int code, int val) {
  input_event ie{};
  ie.type = type;
  ie.code = code;
  ie.value = val;
  int result = -1;
  if (fd >= 0) {
    result = static_cast<int>(m_syscalls->write(fd, &ie, sizeof(ie)));
  }
  if (result < 0) {
    spdlog::error("Error writing to uinput device: {}", errno);
  }
}

void TestRunnerServer::close_input_device(int fd) {
  if (fd < 0) {
    return;
  }
  if (m_syscalls->ioctl(fd, UI_DEV_DESTROY, nullptr) < 0) {
    spdlog::error("error closing device");
  }
  if (m_syscalls->close(fd) < 0) {
    spdlog::error("error closing uinput device");
  }
}

// Recorder interface

#if ENABLE_RECORDER

kj::Promise<void> TestRunnerServer::getSnapshot(GetSnapshotContext context) {
  std::string path = snapshot_recorder->Stop();
  auto ret = context.getResults();
  ret.setPath(path);
  snapshot_recorder->Start();
  return kj::READY_NOW;
}

kj::Promise<void> TestRunnerServer::createCustomRecorder(
    CreateCustomRecorderContext context) {
  auto params = context.getParams();
  auto ret = context.getResults();
  std::string filename = params.getFilename().cStr();
  if (filename.find('/') != std::string::npos ||
      filename.find("..") != std::string::npos) {
    ret.setIndex(255);
    return kj::READY_NOW;
  }
  if (custom_recorders.size() < MAX_CUSTOM_RECORDERS) {
    custom_recorders.push_back(std::make_unique<TestRunnerRecorder>(
        (char*)filename.c_str(), params.getLength(), params.getContinuous()));
    ret.setIndex(custom_recorders.size() - 1);
  } else {
    ret.setIndex(255);
  }
  return kj::READY_NOW;
}

kj::Promise<void> TestRunnerServer::listDevices(ListDevicesContext context) {
  auto index = context.getParams().getIndex();
  if (index < custom_recorders.size()) {
    std::vector<in_device_s> devices = custom_recorders[index]->listDevices();
    auto ret = context.getResults();
    auto device_list = ret.initDevices(devices.size());
    for (size_t i = 0; i < devices.size(); i++) {
      device_list[i].setFd(devices[i].fd);
      device_list[i].setName(devices[i].name);
      device_list[i].setType(devices[i].type);
    }
  }
  return kj::READY_NOW;
}

kj::Promise<void> TestRunnerServer::setDevicesToRecord(
    SetDevicesToRecordContext context) {
  auto params = context.getParams();
  auto index = params.getIndex();
  auto devices_in = params.getDevices();
  if (index < custom_recorders.size()) {
    std::vector<in_device_s> device_list = {};
    for (const auto& dev_in : devices_in) {
      in_device_s indev{};
      indev.fd = static_cast<int>(dev_in.getFd());
      strncpy(indev.name, dev_in.getName().cStr(), sizeof(indev.name));
      indev.name[sizeof(indev.name) - 1] = '\0';
      if (dev_in.getType() >= UNKNOWN) {
        indev.type = UNKNOWN;
      } else {
        indev.type = (device_type)dev_in.getType();
      }
      device_list.push_back(indev);
    }
    custom_recorders[index]->setDevicesToRecord(device_list);
  }
  return kj::READY_NOW;
}

kj::Promise<void> TestRunnerServer::startCustomRecording(
    StartCustomRecordingContext context) {
  auto index = context.getParams().getIndex();
  if (index < custom_recorders.size()) {
    custom_recorders[index]->Start();
  }
  return kj::READY_NOW;
}

kj::Promise<void> TestRunnerServer::stopCustomRecording(
    StopCustomRecordingContext context) {
  auto index = context.getParams().getIndex();
  if (index < custom_recorders.size()) {
    std::string path = custom_recorders[index]->Stop();
    auto ret = context.getResults();
    ret.setPath(path);
  }
  return kj::READY_NOW;
}

kj::Promise<void> TestRunnerServer::checkRecorderActive(
    CheckRecorderActiveContext context) {
  auto index = context.getParams().getIndex();
  auto ret = context.getResults();
  if (index < custom_recorders.size()) {
    ret.setActive(custom_recorders[index]->isActive());
  }
  return kj::READY_NOW;
}

#endif  // ENABLE_RECORDER
