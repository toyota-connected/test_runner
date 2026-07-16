// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <mutex>
#include <string>
#include "capnp/test_runner_server.capnp.h"

class PluginAglHealth : virtual public TestRunnerService::Server {
 public:
  explicit PluginAglHealth(std::string daemon_url = "http://127.0.0.1:7777");
  ~PluginAglHealth() = default;

  kj::Promise<void> getMetrics(GetMetricsContext context) override;
  kj::Promise<void> getMemory(GetMemoryContext context) override;
  kj::Promise<void> getCpu(GetCpuContext context) override;
  kj::Promise<void> getProcesses(GetProcessesContext context) override;
  kj::Promise<void> getNetwork(GetNetworkContext context) override;
  kj::Promise<void> getSecurity(GetSecurityContext context) override;
  kj::Promise<void> getScheduler(GetSchedulerContext context) override;
  kj::Promise<void> setDaemonUrl(SetDaemonUrlContext context) override;

 private:
  std::string fetchJson(const std::string& path);
  std::string daemonUrl();

  mutable std::mutex url_mutex_;
  std::string daemon_url_;
};
