# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3

@0x92d89aa186af2b15;

struct MouseMove {
  relX @0 :Int32;
  relY @1 :Int32;
}

struct MouseClick {
  btn @0 :UInt16;
  pressed @1 :Bool;
}

struct KeyPress {
  pressed @0 :Bool;
  union {
    rawCode @1 :UInt16;  # raw Linux KEY_* evdev code
    keySym  @2 :UInt32;  # XKB keysym (e.g. XKB_KEY_a = 0x61)
    keyName @3 :Text;    # XKB keysym name (e.g. "Return", "space", "a")
  }
}

struct SingleTouch {
  absX @0 :Int32;
  absY @1 :Int32;
  pressed @2 :Bool;
}

struct MultiTouch {
  slot @0 :Int8;
  trackingId @1 :Int16;
  absX @2 :Int32;
  absY @3 :Int32;
}

struct PassThrough {
  device @0 :UInt32;
  type @1 :UInt32;
  code @2 :UInt32;
  value @3 :Int32;
}

interface Input {
  enum Device {
    mouse @0;
    keyboard @1;
    touchscreen @2;
    multiTouchscreen @3;
  }

  enum Command {
    mouseMove @0;
    mouseClick @1;
    keyPress @2;
    singleTouch @3;
    multiTouch @4;
    passThrough @5;
  }

  setDevice @0 (device :Device) -> ();
  setCommand @1 (command :Command) -> ();
  handleMouseMove @2 (mouseMove :MouseMove) -> ();
  handleMouseClick @3 (mouseClick :MouseClick) -> ();
  handleKeyPress @4 (keyPress :KeyPress) -> ();
  handleSingleTouch @5 (singleTouch :SingleTouch) -> ();
  handleMultiTouch @6 (multiTouch :List(MultiTouch)) -> ();
  handlePassThrough @7 (passThrough :PassThrough) -> ();
}

struct InputDevice {
  fd @0 :UInt32;
  name @1 :Text;
  type @2 :UInt8;
}

interface Recorder {
  getSnapshot @0 () -> (path :Text);
  createCustomRecorder @1 (filename :Text, length :UInt32, continuous :Bool) -> (index :UInt8);
  listDevices @2 (index :UInt8) -> (devices :List(InputDevice));
  setDevicesToRecord @3 (index :UInt8, devices :List(InputDevice)) -> ();
  startCustomRecording @4 (index :UInt8) -> ();
  stopCustomRecording @5 (index :UInt8) -> (path :Text);
  checkRecorderActive @6 (index :UInt8) -> (active :Bool);
}