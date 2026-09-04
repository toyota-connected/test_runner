// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "RPCClient.h"
#include "capnp/test_runner_server.capnp.h"

// ---------------------------------------------------------------------------
// Plain-C++ result types — mirror the capnp structs for caller convenience.
// ---------------------------------------------------------------------------

struct AglMemorySnapshot {
  uint64_t total_bytes{0};
  uint64_t free_bytes{0};
  uint64_t cached_bytes{0};
  uint64_t buffered_bytes{0};
  uint64_t slab_bytes{0};
  uint64_t swap_used_bytes{0};
  uint64_t swap_free_bytes{0};
  uint64_t page_faults_minor{0};
  uint64_t page_faults_major{0};
  uint32_t psi_some_pct_x100{0};
  uint32_t psi_full_pct_x100{0};
  uint64_t oom_kills_total{0};
};

struct AglCoreStats {
  uint32_t cpu_id{0};
  uint64_t user_ns{0};
  uint64_t system_ns{0};
  uint64_t iowait_ns{0};
  uint64_t irq_ns{0};
  uint64_t softirq_ns{0};
  uint64_t idle_ns{0};
  uint64_t ctx_switches{0};
};

struct AglCpuSnapshot {
  double load_1{0.0};
  double load_5{0.0};
  double load_15{0.0};
  std::vector<AglCoreStats> cores;
};

struct AglProcessStats {
  uint32_t pid{0};
  uint32_t ppid{0};
  uint32_t uid{0};
  uint32_t thread_count{0};
  uint64_t cpu_user_ns{0};
  uint64_t cpu_system_ns{0};
  uint64_t mem_rss_bytes{0};
  uint64_t mem_vms_bytes{0};
  uint64_t voluntary_ctx_sw{0};
  uint64_t involuntary_ctx_sw{0};
  uint64_t read_bytes{0};
  uint64_t write_bytes{0};
  uint64_t page_faults_minor{0};
  uint64_t page_faults_major{0};
  uint64_t start_time_ns{0};
  uint32_t open_fds{0};
  std::string comm;
};

struct AglNetIfaceStats {
  uint32_t iface_idx{0};
  std::string name;
  uint64_t rx_bytes{0};
  uint64_t tx_bytes{0};
  uint64_t rx_packets{0};
  uint64_t tx_packets{0};
  uint64_t rx_drops{0};
  uint64_t tx_drops{0};
  uint64_t rx_errors{0};
  uint64_t tx_errors{0};
};

struct AglTcpStateSnapshot {
  uint64_t established{0};
  uint64_t syn_sent{0};
  uint64_t syn_recv{0};
  uint64_t fin_wait1{0};
  uint64_t fin_wait2{0};
  uint64_t time_wait{0};
  uint64_t close_wait{0};
  uint64_t listen{0};
  uint64_t listen_overflows{0};
  uint64_t retransmits{0};
  uint64_t resets_in{0};
  uint64_t resets_out{0};
};

struct AglNetworkSnapshot {
  std::vector<AglNetIfaceStats> ifaces;
  AglTcpStateSnapshot tcp;
};

struct AglSecuritySnapshot {
  uint64_t ptrace{0};
  uint64_t memfd_create{0};
  uint64_t prctl{0};
  uint64_t setuid{0};
  uint64_t exec_anomaly{0};
  uint64_t capability_use{0};
};

struct AglSchedBuckets {
  uint64_t lt_10us{0};
  uint64_t lt_100us{0};
  uint64_t lt_1ms{0};
  uint64_t lt_10ms{0};
  uint64_t lt_100ms{0};
  uint64_t lt_1s{0};
  uint64_t lt_10s{0};
  uint64_t ge_10s{0};
};

struct AglSchedulerSnapshot {
  AglSchedBuckets histogram;
  uint64_t total_count{0};
  uint64_t total_lat_ns{0};
  uint64_t max_lat_ns{0};
  uint64_t p50_ns{0};
  uint64_t p95_ns{0};
  uint64_t p99_ns{0};
};

struct AglMetricSnapshot {
  uint64_t timestamp_ns{0};
  AglMemorySnapshot memory;
  AglCpuSnapshot cpu;
  std::vector<AglProcessStats> processes;
  AglNetworkSnapshot network;
  AglSecuritySnapshot security;
  AglSchedulerSnapshot scheduler;
};

// ---------------------------------------------------------------------------
// Client class
// ---------------------------------------------------------------------------

class PluginAglHealthClient : virtual public RPCClient {
 public:
  PluginAglHealthClient() = default;
  ~PluginAglHealthClient() = default;

  AglMetricSnapshot GetMetrics();
  AglMemorySnapshot GetMemory();
  AglCpuSnapshot GetCpu();
  std::vector<AglProcessStats> GetProcesses(uint32_t limit = 100);
  AglNetworkSnapshot GetNetwork();
  AglSecuritySnapshot GetSecurity();
  AglSchedulerSnapshot GetScheduler();
  void SetDaemonUrl(const std::string& url);
};
