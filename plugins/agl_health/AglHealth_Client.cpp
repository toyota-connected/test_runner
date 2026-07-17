// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include "AglHealth_Client.h"
#include <kj/debug.h>
#include "spdlog/spdlog.h"

// ---------------------------------------------------------------------------
// Decode helpers — capnp reader → plain C++ struct
// ---------------------------------------------------------------------------

static AglMemorySnapshot decode_memory(::MemorySnapshot::Reader r) {
  AglMemorySnapshot m;
  m.total_bytes        = r.getTotalBytes();
  m.free_bytes         = r.getFreeBytes();
  m.cached_bytes       = r.getCachedBytes();
  m.buffered_bytes     = r.getBufferedBytes();
  m.slab_bytes         = r.getSlabBytes();
  m.swap_used_bytes    = r.getSwapUsedBytes();
  m.swap_free_bytes    = r.getSwapFreeBytes();
  m.page_faults_minor  = r.getPageFaultsMinor();
  m.page_faults_major  = r.getPageFaultsMajor();
  m.psi_some_pct_x100  = r.getPsiSomePctX100();
  m.psi_full_pct_x100  = r.getPsiFullPctX100();
  m.oom_kills_total    = r.getOomKillsTotal();
  return m;
}

static AglCpuSnapshot decode_cpu(::CpuSnapshot::Reader r) {
  AglCpuSnapshot cpu;
  cpu.load_1  = r.getLoad1();
  cpu.load_5  = r.getLoad5();
  cpu.load_15 = r.getLoad15();
  for (auto core : r.getCores()) {
    AglCoreStats c;
    c.cpu_id       = core.getCpuId();
    c.user_ns      = core.getUserNs();
    c.system_ns    = core.getSystemNs();
    c.iowait_ns    = core.getIowaitNs();
    c.irq_ns       = core.getIrqNs();
    c.softirq_ns   = core.getSoftirqNs();
    c.idle_ns      = core.getIdleNs();
    c.ctx_switches = core.getCtxSwitches();
    cpu.cores.push_back(c);
  }
  return cpu;
}

static std::vector<AglProcessStats> decode_processes(
    capnp::List<::ProcessStats>::Reader list) {
  std::vector<AglProcessStats> out;
  out.reserve(list.size());
  for (auto p : list) {
    AglProcessStats s;
    s.pid               = p.getPid();
    s.ppid              = p.getPpid();
    s.uid               = p.getUid();
    s.thread_count      = p.getThreadCount();
    s.cpu_user_ns       = p.getCpuUserNs();
    s.cpu_system_ns     = p.getCpuSystemNs();
    s.mem_rss_bytes     = p.getMemRssBytes();
    s.mem_vms_bytes     = p.getMemVmsBytes();
    s.voluntary_ctx_sw  = p.getVoluntaryCtxSw();
    s.involuntary_ctx_sw= p.getInvoluntaryCtxSw();
    s.read_bytes        = p.getReadBytes();
    s.write_bytes       = p.getWriteBytes();
    s.page_faults_minor = p.getPageFaultsMinor();
    s.page_faults_major = p.getPageFaultsMajor();
    s.start_time_ns     = p.getStartTimeNs();
    s.open_fds          = p.getOpenFds();
    s.comm              = p.getComm();
    out.push_back(std::move(s));
  }
  return out;
}

static AglNetworkSnapshot decode_network(::NetworkSnapshot::Reader r) {
  AglNetworkSnapshot net;
  for (auto iface : r.getIfaces()) {
    AglNetIfaceStats s;
    s.iface_idx  = iface.getIfaceIdx();
    s.name       = iface.getName();
    s.rx_bytes   = iface.getRxBytes();
    s.tx_bytes   = iface.getTxBytes();
    s.rx_packets = iface.getRxPackets();
    s.tx_packets = iface.getTxPackets();
    s.rx_drops   = iface.getRxDrops();
    s.tx_drops   = iface.getTxDrops();
    s.rx_errors  = iface.getRxErrors();
    s.tx_errors  = iface.getTxErrors();
    net.ifaces.push_back(std::move(s));
  }
  auto tcp = r.getTcp();
  net.tcp.established     = tcp.getEstablished();
  net.tcp.syn_sent        = tcp.getSynSent();
  net.tcp.syn_recv        = tcp.getSynRecv();
  net.tcp.fin_wait1       = tcp.getFinWait1();
  net.tcp.fin_wait2       = tcp.getFinWait2();
  net.tcp.time_wait       = tcp.getTimeWait();
  net.tcp.close_wait      = tcp.getCloseWait();
  net.tcp.listen          = tcp.getListen();
  net.tcp.listen_overflows= tcp.getListenOverflows();
  net.tcp.retransmits     = tcp.getRetransmits();
  net.tcp.resets_in       = tcp.getResetsIn();
  net.tcp.resets_out      = tcp.getResetsOut();
  return net;
}

static AglSecuritySnapshot decode_security(::SecuritySnapshot::Reader r) {
  AglSecuritySnapshot s;
  s.ptrace        = r.getPtrace();
  s.memfd_create  = r.getMemfdCreate();
  s.prctl         = r.getPrctl();
  s.setuid        = r.getSetuid();
  s.exec_anomaly  = r.getExecAnomaly();
  s.capability_use= r.getCapabilityUse();
  return s;
}

static AglSchedulerSnapshot decode_scheduler(::SchedulerSnapshot::Reader r) {
  AglSchedulerSnapshot s;
  auto h = r.getHistogram();
  s.histogram.lt_10us  = h.getLt10Us();
  s.histogram.lt_100us = h.getLt100Us();
  s.histogram.lt_1ms   = h.getLt1Ms();
  s.histogram.lt_10ms  = h.getLt10Ms();
  s.histogram.lt_100ms = h.getLt100Ms();
  s.histogram.lt_1s    = h.getLt1S();
  s.histogram.lt_10s   = h.getLt10S();
  s.histogram.ge_10s   = h.getGe10S();
  s.total_count  = r.getTotalCount();
  s.total_lat_ns = r.getTotalLatNs();
  s.max_lat_ns   = r.getMaxLatNs();
  s.p50_ns       = r.getP50Ns();
  s.p95_ns       = r.getP95Ns();
  s.p99_ns       = r.getP99Ns();
  return s;
}

// ---------------------------------------------------------------------------
// RPC calls
// ---------------------------------------------------------------------------

AglMetricSnapshot PluginAglHealthClient::GetMetrics() {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto client = getMain<AglHealth>();
  auto response = waitWithTimeout(client.getMetricsRequest().send());
  auto snap = response.getSnapshot();
  AglMetricSnapshot out;
  out.timestamp_ns = snap.getTimestampNs();
  out.memory    = decode_memory(snap.getMemory());
  out.cpu       = decode_cpu(snap.getCpu());
  out.processes = decode_processes(snap.getProcesses());
  out.network   = decode_network(snap.getNetwork());
  out.security  = decode_security(snap.getSecurity());
  out.scheduler = decode_scheduler(snap.getScheduler());
  return out;
}

AglMemorySnapshot PluginAglHealthClient::GetMemory() {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto client = getMain<AglHealth>();
  auto response = waitWithTimeout(client.getMemoryRequest().send());
  return decode_memory(response.getMemory());
}

AglCpuSnapshot PluginAglHealthClient::GetCpu() {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto client = getMain<AglHealth>();
  auto response = waitWithTimeout(client.getCpuRequest().send());
  return decode_cpu(response.getCpu());
}

std::vector<AglProcessStats> PluginAglHealthClient::GetProcesses(uint32_t limit) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto client = getMain<AglHealth>();
  auto req = client.getProcessesRequest();
  req.setLimit(limit);
  auto response = waitWithTimeout(req.send());
  return decode_processes(response.getProcesses());
}

AglNetworkSnapshot PluginAglHealthClient::GetNetwork() {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto client = getMain<AglHealth>();
  auto response = waitWithTimeout(client.getNetworkRequest().send());
  return decode_network(response.getNetwork());
}

AglSecuritySnapshot PluginAglHealthClient::GetSecurity() {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto client = getMain<AglHealth>();
  auto response = waitWithTimeout(client.getSecurityRequest().send());
  return decode_security(response.getSecurity());
}

AglSchedulerSnapshot PluginAglHealthClient::GetScheduler() {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto client = getMain<AglHealth>();
  auto response = waitWithTimeout(client.getSchedulerRequest().send());
  return decode_scheduler(response.getScheduler());
}

void PluginAglHealthClient::SetDaemonUrl(const std::string& url) {
  KJ_REQUIRE(isConnected, "Not connected to Test Runner server");
  auto client = getMain<AglHealth>();
  auto req = client.setDaemonUrlRequest();
  req.setUrl(url);
  waitWithTimeout(req.send());
  spdlog::debug("AglHealth daemon URL set to: {}", url);
}
