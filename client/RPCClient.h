// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <capnp/rpc-twoparty.h>
#include <kj/async-io.h>
#include <kj/debug.h>
#include <memory>

static constexpr kj::Duration RPC_TIMEOUT = 10 * kj::SECONDS;

class RPCClient {
 public:
  RPCClient() = default;
  ~RPCClient() = default;

  void Connect(const char* server_ip) {
    conn = std::make_unique<Connection>(server_ip);
    last_server_ip = server_ip;
    isConnected = true;
  }

  template <typename T>
  typename T::Client getMain() {
    return conn->client.bootstrap().castAs<T>();
  }

  kj::WaitScope& getWaitScope() { return conn->io.waitScope; }
  kj::Timer& getTimer() { return conn->io.provider->getTimer(); }

  template <typename Results>
  capnp::Response<Results> waitWithTimeout(
      capnp::RemotePromise<Results> promise) {
    kj::Promise<capnp::Response<Results>> rpc_promise = kj::mv(promise);
    auto timeout = getTimer()
                       .afterDelay(RPC_TIMEOUT)
                       .then([]() -> capnp::Response<Results> {
                         KJ_FAIL_REQUIRE("RPC timed out");
                       });
    try {
      return rpc_promise.exclusiveJoin(kj::mv(timeout)).wait(getWaitScope());
    } catch (kj::Exception& e) {
      if (e.getType() == kj::Exception::Type::DISCONNECTED) {
        conn.reset();
        isConnected = false;
      }
      throw;
    }
  }

 protected:
  bool isConnected = false;
  const char* last_server_ip = nullptr;

 private:
  struct Connection {
    kj::AsyncIoContext io;
    kj::Own<kj::AsyncIoStream> stream;
    capnp::TwoPartyClient client;

    explicit Connection(const char* server_ip)
        : io(kj::setupAsyncIo()),
          stream(io.provider->getNetwork()
                     .parseAddress(server_ip)
                     .wait(io.waitScope)
                     ->connect()
                     .wait(io.waitScope)),
          client(*stream) {}
  };

  std::unique_ptr<Connection> conn;
};
