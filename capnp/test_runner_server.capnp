# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3

@0x92d89aa186af2b18;

using import "test_runner.capnp".Input;
using import "test_runner.capnp".Recorder;
using import "../plugins/plugins.capnp".Plugins;

# Core service interface. To add a plugin, import its capnp interface and add
# it to the extends list, then implement the new methods in TestRunnerServer.
interface TestRunnerService extends (Input, Recorder, Plugins) {
}
