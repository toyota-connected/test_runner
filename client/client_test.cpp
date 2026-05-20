// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include "TestRunnerClient.h"
#include "capnp/test_runner.capnp.h"

#include <spdlog/spdlog.h>

int main() {
  auto client = TestRunnerClient();
  client.Connect("localhost:4004");

  /* Call RPC Methods */

  client.WaitForTasks();
}
