// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include <gtest/gtest.h>

#include "wayland_screenshooter/WaylandCapture.h"

namespace {

WaylandCaptureBase make_state(
    std::initializer_list<std::pair<const char*, const char*>> entries) {
  WaylandCaptureBase s;
  for (auto& [name, model] : entries) {
    WaylandOutputInfo info;
    info.output = nullptr;
    info.name = name;
    info.model = model;
    info.width = 1920;
    info.height = 1080;
    s.outputs.push_back(info);
  }
  return s;
}

}  // namespace

TEST(SelectOutput, NoOutputsReturnsNull) {
  WaylandCaptureBase s;
  EXPECT_EQ(select_output(&s, ""), nullptr);
  EXPECT_EQ(select_output(&s, "HDMI-A-1"), nullptr);
}

TEST(SelectOutput, EmptyPreferredReturnsFirst) {
  auto s = make_state({{"HDMI-A-1", "SomeTV"}, {"DP-1", "Monitor"}});
  auto* out = select_output(&s, "");
  ASSERT_NE(out, nullptr);
  EXPECT_EQ(out->name, "HDMI-A-1");
}

TEST(SelectOutput, MatchesByConnectorNameNotEdidModel) {
  auto s = make_state({{"HDMI-A-1", "SomeTV"}});
  EXPECT_NE(select_output(&s, "HDMI-A-1"), nullptr);
  EXPECT_EQ(select_output(&s, "SomeTV"), nullptr);
}

TEST(SelectOutput, NoMatchReturnsNullNotFirstOutput) {
  auto s = make_state({{"HDMI-A-1", "SomeTV"}, {"DP-1", "Monitor"}});
  EXPECT_EQ(select_output(&s, "VGA-1"), nullptr);
}

TEST(SelectOutput, MatchNonFirstOutput) {
  auto s = make_state({{"HDMI-A-1", "SomeTV"}, {"DP-1", "Monitor"}});
  auto* out = select_output(&s, "DP-1");
  ASSERT_NE(out, nullptr);
  EXPECT_EQ(out->name, "DP-1");
}

TEST(SelectOutput, MatchIsExactCaseSensitive) {
  auto s = make_state({{"HDMI-A-1", "SomeTV"}});
  EXPECT_EQ(select_output(&s, "HDMI-A"), nullptr);
  EXPECT_EQ(select_output(&s, "hdmi-a-1"), nullptr);
  EXPECT_NE(select_output(&s, "HDMI-A-1"), nullptr);
}
