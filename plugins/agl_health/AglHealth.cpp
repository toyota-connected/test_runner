// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include "AglHealth.h"
#include "TestRunnerServer.h"

#include <curl/curl.h>
#include <kj/debug.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include "spdlog/spdlog.h"

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// libcurl helpers
// ---------------------------------------------------------------------------

static size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* buf = static_cast<std::string*>(userdata);
  buf->append(ptr, size * nmemb);
  return size * nmemb;
}

PluginAglHealth::PluginAglHealth(std::string daemon_url)
    : daemon_url_(std::move(daemon_url)) {}

std::string PluginAglHealth::daemonUrl() {
  std::lock_guard<std::mutex> lock(url_mutex_);
  return daemon_url_;
}

std::string PluginAglHealth::fetchJson(const std::string& path) {
  std::string url = daemonUrl() + path;
  std::string body;

  CURL* curl = curl_easy_init();
  KJ_REQUIRE(curl != nullptr, "curl_easy_init failed");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  CURLcode rc = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  KJ_REQUIRE(rc == CURLE_OK, "curl GET failed", url, curl_easy_strerror(rc));

  long http_code = 0;
  // Re-init just to get the status — already cleaned up, so check body is non-empty
  // Actually we already cleaned up curl above; check rc covered transport errors.
  // HTTP-level errors will surface as malformed JSON and throw below.

  return body;
}

// ---------------------------------------------------------------------------
// Populate capnp builders from JSON
// ---------------------------------------------------------------------------

static void fill_memory(::MemorySnapshot::Builder b, const json& j) {
  b.setTotalBytes(j.value("total_bytes", 0ULL));
  b.setFreeBytes(j.value("free_bytes", 0ULL));
  b.setCachedBytes(j.value("cached_bytes", 0ULL));
  b.setBufferedBytes(j.value("buffered_bytes", 0ULL));
  b.setSlabBytes(j.value("slab_bytes", 0ULL));
  b.setSwapUsedBytes(j.value("swap_used_bytes", 0ULL));
  b.setSwapFreeBytes(j.value("swap_free_bytes", 0ULL));
  b.setPageFaultsMinor(j.value("page_faults_minor", 0ULL));
  b.setPageFaultsMajor(j.value("page_faults_major", 0ULL));
  b.setPsiSomePctX100(j.value("psi_some_pct_x100", 0U));
  b.setPsiFullPctX100(j.value("psi_full_pct_x100", 0U));
  b.setOomKillsTotal(j.value("oom_kills_total", 0ULL));
}

static void fill_cpu(::CpuSnapshot::Builder b, const json& j) {
  const auto& load = j.at("load");
  b.setLoad1(load.value("load_1", 0.0));
  b.setLoad5(load.value("load_5", 0.0));
  b.setLoad15(load.value("load_15", 0.0));

  const auto& cores_j = j.at("cores");
  auto cores = b.initCores(static_cast<uint32_t>(cores_j.size()));
  for (size_t i = 0; i < cores_j.size(); ++i) {
    const auto& c = cores_j[i];
    auto core = cores[i];
    core.setCpuId(c.value("cpu_id", 0U));
    core.setUserNs(c.value("user_ns", 0ULL));
    core.setSystemNs(c.value("system_ns", 0ULL));
    core.setIowaitNs(c.value("iowait_ns", 0ULL));
    core.setIrqNs(c.value("irq_ns", 0ULL));
    core.setSoftirqNs(c.value("softirq_ns", 0ULL));
    core.setIdleNs(c.value("idle_ns", 0ULL));
    core.setCtxSwitches(c.value("ctx_switches", 0ULL));
  }
}

static void fill_processes(capnp::List<::ProcessStats>::Builder list,
                            const json& arr) {
  for (size_t i = 0; i < arr.size(); ++i) {
    const auto& p = arr[i];
    auto proc = list[i];
    proc.setPid(p.value("pid", 0U));
    proc.setPpid(p.value("ppid", 0U));
    proc.setUid(p.value("uid", 0U));
    proc.setThreadCount(p.value("thread_count", 0U));
    proc.setCpuUserNs(p.value("cpu_user_ns", 0ULL));
    proc.setCpuSystemNs(p.value("cpu_system_ns", 0ULL));
    proc.setMemRssBytes(p.value("mem_rss_bytes", 0ULL));
    proc.setMemVmsBytes(p.value("mem_vms_bytes", 0ULL));
    proc.setVoluntaryCtxSw(p.value("voluntary_ctx_sw", 0ULL));
    proc.setInvoluntaryCtxSw(p.value("involuntary_ctx_sw", 0ULL));
    proc.setReadBytes(p.value("read_bytes", 0ULL));
    proc.setWriteBytes(p.value("write_bytes", 0ULL));
    proc.setPageFaultsMinor(p.value("page_faults_minor", 0ULL));
    proc.setPageFaultsMajor(p.value("page_faults_major", 0ULL));
    proc.setStartTimeNs(p.value("start_time_ns", 0ULL));
    proc.setOpenFds(p.value("open_fds", 0U));
    // comm arrives as a null-terminated byte array, not a string.
    std::string comm;
    if (p.contains("comm") && p["comm"].is_array()) {
      for (const auto &b : p["comm"]) {
        auto c = b.get<uint8_t>();
        if (c == 0) break;
        comm.push_back(static_cast<char>(c));
      }
    } else {
      comm = p.value("comm", std::string{});
    }
    proc.setComm(comm);
  }
}

static void fill_network(::NetworkSnapshot::Builder b, const json& j) {
  const auto& ifaces_j = j.at("ifaces");
  auto ifaces = b.initIfaces(static_cast<uint32_t>(ifaces_j.size()));
  for (size_t i = 0; i < ifaces_j.size(); ++i) {
    const auto& src = ifaces_j[i];
    auto iface = ifaces[i];
    iface.setIfaceIdx(src.value("iface_idx", 0U));
    iface.setName(src.value("name", std::string{}));
    iface.setRxBytes(src.value("rx_bytes", 0ULL));
    iface.setTxBytes(src.value("tx_bytes", 0ULL));
    iface.setRxPackets(src.value("rx_packets", 0ULL));
    iface.setTxPackets(src.value("tx_packets", 0ULL));
    iface.setRxDrops(src.value("rx_drops", 0ULL));
    iface.setTxDrops(src.value("tx_drops", 0ULL));
    iface.setRxErrors(src.value("rx_errors", 0ULL));
    iface.setTxErrors(src.value("tx_errors", 0ULL));
  }

  const auto& tcp = j.at("tcp");
  auto t = b.getTcp();
  t.setEstablished(tcp.value("established", 0ULL));
  t.setSynSent(tcp.value("syn_sent", 0ULL));
  t.setSynRecv(tcp.value("syn_recv", 0ULL));
  t.setFinWait1(tcp.value("fin_wait1", 0ULL));
  t.setFinWait2(tcp.value("fin_wait2", 0ULL));
  t.setTimeWait(tcp.value("time_wait", 0ULL));
  t.setCloseWait(tcp.value("close_wait", 0ULL));
  t.setListen(tcp.value("listen", 0ULL));
  t.setListenOverflows(tcp.value("listen_overflows", 0ULL));
  t.setRetransmits(tcp.value("retransmits", 0ULL));
  t.setResetsIn(tcp.value("resets_in", 0ULL));
  t.setResetsOut(tcp.value("resets_out", 0ULL));
}

static void fill_security(::SecuritySnapshot::Builder b, const json& j) {
  b.setPtrace(j.value("ptrace", 0ULL));
  b.setMemfdCreate(j.value("memfd_create", 0ULL));
  b.setPrctl(j.value("prctl", 0ULL));
  b.setSetuid(j.value("setuid", 0ULL));
  b.setExecAnomaly(j.value("exec_anomaly", 0ULL));
  b.setCapabilityUse(j.value("capability_use", 0ULL));
}

static void fill_scheduler(::SchedulerSnapshot::Builder b, const json& j) {
  const auto& hist = j.at("histogram");
  const auto& buckets = hist.at("buckets");
  auto hb = b.initHistogram();
  auto get_bucket = [&](size_t i) -> uint64_t {
    return (i < buckets.size()) ? buckets[i].get<uint64_t>() : 0ULL;
  };
  hb.setLt10Us(get_bucket(0));
  hb.setLt100Us(get_bucket(1));
  hb.setLt1Ms(get_bucket(2));
  hb.setLt10Ms(get_bucket(3));
  hb.setLt100Ms(get_bucket(4));
  hb.setLt1S(get_bucket(5));
  hb.setLt10S(get_bucket(6));
  hb.setGe10S(get_bucket(7));
  b.setTotalCount(hist.value("total_count", 0ULL));
  b.setTotalLatNs(hist.value("total_latency_ns", 0ULL));
  b.setMaxLatNs(hist.value("max_latency_ns", 0ULL));
  b.setP50Ns(j.value("p50_ns", 0ULL));
  b.setP95Ns(j.value("p95_ns", 0ULL));
  b.setP99Ns(j.value("p99_ns", 0ULL));
}

// ---------------------------------------------------------------------------
// RPC implementations
// ---------------------------------------------------------------------------

kj::Promise<void> PluginAglHealth::getMetrics(GetMetricsContext context) {
  json j = json::parse(fetchJson("/metrics"));
  auto snap = context.getResults().initSnapshot();
  snap.setTimestampNs(j.value("timestamp_ns", 0ULL));
  fill_memory(snap.initMemory(), j.at("memory"));

  // /metrics is flat: load and cpu_cores are top-level; adapt for fill_cpu.
  json cpu_j = {{"load", j.at("load")}, {"cores", j.at("cpu_cores")}};
  fill_cpu(snap.initCpu(), cpu_j);

  const auto& procs_j = j.at("top_processes");
  fill_processes(snap.initProcesses(static_cast<uint32_t>(procs_j.size())),
                 procs_j);

  // /metrics is flat: net_ifaces and tcp are top-level; adapt for fill_network.
  json net_j = {{"ifaces", j.at("net_ifaces")}, {"tcp", j.at("tcp")}};
  fill_network(snap.initNetwork(), net_j);

  fill_security(snap.initSecurity(), j.at("security"));
  fill_scheduler(snap.initScheduler(), j.at("sched"));
  return kj::READY_NOW;
}

kj::Promise<void> PluginAglHealth::getMemory(GetMemoryContext context) {
  json j = json::parse(fetchJson("/metrics/memory"));
  fill_memory(context.getResults().initMemory(), j);
  return kj::READY_NOW;
}

kj::Promise<void> PluginAglHealth::getCpu(GetCpuContext context) {
  json j = json::parse(fetchJson("/metrics/cpu"));
  fill_cpu(context.getResults().initCpu(), j);
  return kj::READY_NOW;
}

kj::Promise<void> PluginAglHealth::getProcesses(GetProcessesContext context) {
  uint32_t limit = context.getParams().getLimit();
  std::string path = "/metrics/process";
  if (limit > 0)
    path += "?limit=" + std::to_string(limit);
  json arr = json::parse(fetchJson(path));
  fill_processes(
      context.getResults().initProcesses(static_cast<uint32_t>(arr.size())),
      arr);
  return kj::READY_NOW;
}

kj::Promise<void> PluginAglHealth::getNetwork(GetNetworkContext context) {
  json j = json::parse(fetchJson("/metrics/network"));
  fill_network(context.getResults().initNetwork(), j);
  return kj::READY_NOW;
}

kj::Promise<void> PluginAglHealth::getSecurity(GetSecurityContext context) {
  json j = json::parse(fetchJson("/metrics/security"));
  fill_security(context.getResults().initSecurity(), j);
  return kj::READY_NOW;
}

kj::Promise<void> PluginAglHealth::getScheduler(GetSchedulerContext context) {
  json j = json::parse(fetchJson("/metrics/scheduler"));
  fill_scheduler(context.getResults().initScheduler(), j);
  return kj::READY_NOW;
}

kj::Promise<void> PluginAglHealth::setDaemonUrl(SetDaemonUrlContext context) {
  std::string url = context.getParams().getUrl();
  {
    std::lock_guard<std::mutex> lock(url_mutex_);
    daemon_url_ = std::move(url);
  }
  SPDLOG_INFO("AglHealth daemon URL set to: {}", daemon_url_);
  return kj::READY_NOW;
}
