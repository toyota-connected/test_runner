# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
@0xa2b3c4d5e6f70001;

# Plugin: AglHealth
# Provides access to agl-health-daemon metrics via its REST API
# (default: http://127.0.0.1:7777).

interface AglHealth {
  # Full snapshot — all subsystems in one call.
  getMetrics    @0 () -> (snapshot :MetricSnapshot);

  # Per-subsystem convenience methods.
  getMemory     @1 () -> (memory :MemorySnapshot);
  getCpu        @2 () -> (cpu :CpuSnapshot);
  getProcesses  @3 (limit :UInt32 = 100) -> (processes :List(ProcessStats));
  getNetwork    @4 () -> (network :NetworkSnapshot);
  getSecurity   @5 () -> (security :SecuritySnapshot);
  getScheduler  @6 () -> (scheduler :SchedulerSnapshot);

  # Configuration — override the daemon URL at runtime.
  setDaemonUrl  @7 (url :Text) -> ();
}

struct MemorySnapshot {
  totalBytes       @0  :UInt64;
  freeBytes        @1  :UInt64;
  cachedBytes      @2  :UInt64;
  bufferedBytes    @3  :UInt64;
  slabBytes        @4  :UInt64;
  swapUsedBytes    @5  :UInt64;
  swapFreeBytes    @6  :UInt64;
  pageFaultsMinor  @7  :UInt64;
  pageFaultsMajor  @8  :UInt64;
  psiSomePctX100   @9  :UInt32;
  psiFullPctX100   @10 :UInt32;
  oomKillsTotal    @11 :UInt64;
}

struct CpuStats {
  cpuId         @0 :UInt32;
  userNs        @1 :UInt64;
  systemNs      @2 :UInt64;
  iowaitNs      @3 :UInt64;
  irqNs         @4 :UInt64;
  softirqNs     @5 :UInt64;
  idleNs        @6 :UInt64;
  ctxSwitches   @7 :UInt64;
}

struct CpuSnapshot {
  load1          @0 :Float64;
  load5          @1 :Float64;
  load15         @2 :Float64;
  cores          @3 :List(CpuStats);
}

struct ProcessStats {
  pid               @0  :UInt32;
  ppid              @1  :UInt32;
  uid               @2  :UInt32;
  threadCount       @3  :UInt32;
  cpuUserNs         @4  :UInt64;
  cpuSystemNs       @5  :UInt64;
  memRssBytes       @6  :UInt64;
  memVmsBytes       @7  :UInt64;
  voluntaryCtxSw    @8  :UInt64;
  involuntaryCtxSw  @9  :UInt64;
  readBytes         @10 :UInt64;
  writeBytes        @11 :UInt64;
  pageFaultsMinor   @12 :UInt64;
  pageFaultsMajor   @13 :UInt64;
  startTimeNs       @14 :UInt64;
  openFds           @15 :UInt32;
  comm              @16 :Text;
}

struct NetIfaceStats {
  ifaceIdx    @0 :UInt32;
  name        @1 :Text;
  rxBytes     @2 :UInt64;
  txBytes     @3 :UInt64;
  rxPackets   @4 :UInt64;
  txPackets   @5 :UInt64;
  rxDrops     @6 :UInt64;
  txDrops     @7 :UInt64;
  rxErrors    @8 :UInt64;
  txErrors    @9 :UInt64;
}

struct TcpStateSnapshot {
  established      @0  :UInt64;
  synSent          @1  :UInt64;
  synRecv          @2  :UInt64;
  finWait1         @3  :UInt64;
  finWait2         @4  :UInt64;
  timeWait         @5  :UInt64;
  closeWait        @6  :UInt64;
  listen           @7  :UInt64;
  listenOverflows  @8  :UInt64;
  retransmits      @9  :UInt64;
  resetsIn         @10 :UInt64;
  resetsOut        @11 :UInt64;
}

struct NetworkSnapshot {
  ifaces  @0 :List(NetIfaceStats);
  tcp     @1 :TcpStateSnapshot;
}

struct SecuritySnapshot {
  ptrace          @0 :UInt64;
  memfdCreate     @1 :UInt64;
  prctl           @2 :UInt64;
  setuid          @3 :UInt64;
  execAnomaly     @4 :UInt64;
  capabilityUse   @5 :UInt64;
}

struct SchedBuckets {
  lt10Us    @0 :UInt64;
  lt100Us   @1 :UInt64;
  lt1Ms     @2 :UInt64;
  lt10Ms    @3 :UInt64;
  lt100Ms   @4 :UInt64;
  lt1S      @5 :UInt64;
  lt10S     @6 :UInt64;
  ge10S     @7 :UInt64;
}

struct SchedulerSnapshot {
  histogram     @0 :SchedBuckets;
  totalCount    @1 :UInt64;
  totalLatNs    @2 :UInt64;
  maxLatNs      @3 :UInt64;
  p50Ns         @4 :UInt64;
  p95Ns         @5 :UInt64;
  p99Ns         @6 :UInt64;
}

struct MetricSnapshot {
  timestampNs  @0 :UInt64;
  memory       @1 :MemorySnapshot;
  cpu          @2 :CpuSnapshot;
  processes    @3 :List(ProcessStats);
  network      @4 :NetworkSnapshot;
  security     @5 :SecuritySnapshot;
  scheduler    @6 :SchedulerSnapshot;
}
