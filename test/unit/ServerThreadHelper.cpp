// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3

// This translation unit intentionally includes TestRunnerServer.h and NOT TestRunnerClient.h
// to avoid the duplicate definition of device_type/in_device_s/ErrorHandlerImpl.

#include "ServerThreadHelper.h"

#include <fcntl.h>
#include <linux/uinput.h>

#include <capnp/ez-rpc.h>
#include <kj/async.h>

#include "TestRunnerServer.h"

using ::testing::_;
using ::testing::Return;

ServerThread::ServerThread() {
  EXPECT_CALL(mock, open(_, _))
      .WillOnce(Return(10))
      .WillOnce(Return(11))
      .WillOnce(Return(12))
      .WillOnce(Return(13));
  EXPECT_CALL(mock, ioctl_int(_, _, _)).WillRepeatedly(Return(0));
  EXPECT_CALL(mock, ioctl(_, _, _)).WillRepeatedly(Return(0));
  EXPECT_CALL(mock, usleep(_)).WillRepeatedly(Return());
  EXPECT_CALL(mock, close(_)).WillRepeatedly(Return(0));

  thread = std::thread([this] {
    capnp::EzRpcServer server(
        kj::heap<TestRunnerServer>(&mock, false, false), "localhost");
    {
      std::lock_guard<std::mutex> lk(mtx);
      port = static_cast<uint16_t>(
          server.getPort().wait(server.getWaitScope()));
      ready = true;
    }
    cv.notify_one();
    kj::NEVER_DONE.wait(server.getWaitScope());
  });

  std::unique_lock<std::mutex> lk(mtx);
  cv.wait(lk, [this] { return ready; });
}

ServerThread::~ServerThread() {
  thread.detach();
}

std::string ServerThread::address() const {
  return "localhost:" + std::to_string(port);
}
