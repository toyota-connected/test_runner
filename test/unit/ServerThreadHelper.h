// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "MockSyscall.h"

// Runs an EzRpcServer backed by TestRunnerServer in a background thread.
// Defined in ServerThreadHelper.cpp (the only TU that includes TestRunnerServer.h)
// so that test_client.cpp can include TestRunnerClient.h without symbol conflicts.
struct ServerThread {
  MockSyscall mock;
  uint16_t port{0};
  std::mutex mtx;
  std::condition_variable cv;
  bool ready{false};
  std::thread thread;

  ServerThread();
  ~ServerThread();

  std::string address() const;
};
