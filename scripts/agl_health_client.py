# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
#!/usr/bin/env python3
"""Exercise the AglHealth capnp plugin against a running TestRunnerService."""

import argparse
import asyncio
import os

import capnp

capnp.remove_import_hook()
script_dir = os.path.dirname(os.path.abspath(__file__))
repo_root = os.path.normpath(os.path.join(script_dir, '..'))
# Load agl_health.capnp directly — it has no imports so capnp's filesystem
# never encounters ".." path components (which KJ refuses to resolve).
# The server's bootstrap capability implements AglHealth transitively through
# TestRunnerService -> Plugins -> AglHealth, so the cast works fine.
schema_path = os.path.join(repo_root, 'plugins', 'agl_health', 'agl_health.capnp')
agl_health_capnp = capnp.load(schema_path)


def fmt_bytes(n):
    for unit in ('B', 'KiB', 'MiB', 'GiB'):
        if n < 1024:
            return f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} TiB"


def fmt_ns(n):
    if n < 1_000:
        return f"{n} ns"
    if n < 1_000_000:
        return f"{n/1_000:.2f} µs"
    if n < 1_000_000_000:
        return f"{n/1_000_000:.2f} ms"
    return f"{n/1_000_000_000:.3f} s"


def print_memory(m):
    print("=== Memory ===")
    print(f"  Total:         {fmt_bytes(m.totalBytes)}")
    print(f"  Free:          {fmt_bytes(m.freeBytes)}")
    print(f"  Cached:        {fmt_bytes(m.cachedBytes)}")
    print(f"  Buffered:      {fmt_bytes(m.bufferedBytes)}")
    print(f"  Slab:          {fmt_bytes(m.slabBytes)}")
    print(f"  Swap used:     {fmt_bytes(m.swapUsedBytes)}")
    print(f"  Swap free:     {fmt_bytes(m.swapFreeBytes)}")
    print(f"  Page faults (minor/major): {m.pageFaultsMinor} / {m.pageFaultsMajor}")
    print(f"  PSI some:      {m.psiSomePctX100 / 100:.2f}%")
    print(f"  PSI full:      {m.psiFullPctX100 / 100:.2f}%")
    print(f"  OOM kills:     {m.oomKillsTotal}")


def print_cpu(c):
    print("=== CPU ===")
    print(f"  Load avg:  {c.load1:.2f}  {c.load5:.2f}  {c.load15:.2f}  (1/5/15 min)")
    for core in c.cores:
        print(f"  CPU{core.cpuId}: user={fmt_ns(core.userNs)} sys={fmt_ns(core.systemNs)}"
              f" iowait={fmt_ns(core.iowaitNs)} idle={fmt_ns(core.idleNs)}"
              f" ctx={core.ctxSwitches}")


def print_processes(procs):
    print("=== Processes ===")
    print(f"  {'PID':>7}  {'PPID':>7}  {'UID':>5}  {'THR':>4}  {'RSS':>10}  {'FDs':>4}  COMM")
    for p in procs:
        print(f"  {p.pid:>7}  {p.ppid:>7}  {p.uid:>5}  {p.threadCount:>4}"
              f"  {fmt_bytes(p.memRssBytes):>10}  {p.openFds:>4}  {p.comm}")


def print_network(n):
    print("=== Network ===")
    print("  Interfaces:")
    for iface in n.ifaces:
        print(f"    [{iface.ifaceIdx}] {iface.name}: "
              f"rx={fmt_bytes(iface.rxBytes)} ({iface.rxPackets} pkts, {iface.rxErrors} err, {iface.rxDrops} drop) "
              f"tx={fmt_bytes(iface.txBytes)} ({iface.txPackets} pkts, {iface.txErrors} err, {iface.txDrops} drop)")
    t = n.tcp
    print("  TCP states:")
    print(f"    ESTABLISHED={t.established}  SYN_SENT={t.synSent}  SYN_RECV={t.synRecv}")
    print(f"    FIN_WAIT1={t.finWait1}  FIN_WAIT2={t.finWait2}  TIME_WAIT={t.timeWait}  CLOSE_WAIT={t.closeWait}")
    print(f"    LISTEN={t.listen}  listen_overflows={t.listenOverflows}")
    print(f"    retransmits={t.retransmits}  resets_in={t.resetsIn}  resets_out={t.resetsOut}")


def print_security(s):
    print("=== Security ===")
    print(f"  ptrace:         {s.ptrace}")
    print(f"  memfd_create:   {s.memfdCreate}")
    print(f"  prctl:          {s.prctl}")
    print(f"  setuid:         {s.setuid}")
    print(f"  exec_anomaly:   {s.execAnomaly}")
    print(f"  capability_use: {s.capabilityUse}")


def print_scheduler(s):
    print("=== Scheduler ===")
    h = s.histogram
    print(f"  Latency histogram:")
    print(f"    <10µs={h.lt10Us}  <100µs={h.lt100Us}  <1ms={h.lt1Ms}  <10ms={h.lt10Ms}")
    print(f"    <100ms={h.lt100Ms}  <1s={h.lt1S}  <10s={h.lt10S}  >=10s={h.ge10S}")
    print(f"  Total events: {s.totalCount}")
    if s.totalCount:
        print(f"  Avg latency:  {fmt_ns(s.totalLatNs // s.totalCount)}")
    print(f"  Max latency:  {fmt_ns(s.maxLatNs)}")
    print(f"  p50={fmt_ns(s.p50Ns)}  p95={fmt_ns(s.p95Ns)}  p99={fmt_ns(s.p99Ns)}")


COMMANDS = {
    'metrics', 'memory', 'cpu', 'processes', 'network', 'security', 'scheduler',
}


def parse_args():
    parser = argparse.ArgumentParser(
        description="AglHealth capnp plugin client",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"Commands: {', '.join(sorted(COMMANDS))}  (default: metrics)",
    )
    parser.add_argument("--host", default='localhost:4004', help="HOST:PORT (default: localhost:4004)")
    parser.add_argument("--daemon-url", metavar="URL",
                        help="Override the agl-health daemon URL on the server before querying")
    parser.add_argument("--limit", type=int, default=100,
                        help="Max processes to return for 'processes' command (default: 100)")
    parser.add_argument("command", nargs='?', default='metrics',
                        choices=sorted(COMMANDS),
                        help="Which subsystem to query")
    return parser.parse_args()


async def run(connection, args):
    client = capnp.TwoPartyClient(connection)
    server = client.bootstrap().cast_as(agl_health_capnp.AglHealth)

    if args.daemon_url:
        await server.setDaemonUrl(url=args.daemon_url)
        print(f"Daemon URL set to: {args.daemon_url}")

    cmd = args.command
    if cmd == 'metrics':
        resp = await server.getMetrics()
        snap = resp.snapshot
        print(f"Timestamp: {snap.timestampNs} ns")
        print_memory(snap.memory)
        print_cpu(snap.cpu)
        print_processes(snap.processes)
        print_network(snap.network)
        print_security(snap.security)
        print_scheduler(snap.scheduler)
    elif cmd == 'memory':
        resp = await server.getMemory()
        print_memory(resp.memory)
    elif cmd == 'cpu':
        resp = await server.getCpu()
        print_cpu(resp.cpu)
    elif cmd == 'processes':
        resp = await server.getProcesses(limit=args.limit)
        print_processes(resp.processes)
    elif cmd == 'network':
        resp = await server.getNetwork()
        print_network(resp.network)
    elif cmd == 'security':
        resp = await server.getSecurity()
        print_security(resp.security)
    elif cmd == 'scheduler':
        resp = await server.getScheduler()
        print_scheduler(resp.scheduler)


async def cmd_main(args):
    host, port = args.host.rsplit(':', 1)
    conn = await capnp.AsyncIoStream.create_connection(host=host, port=int(port))
    await run(conn, args)


if __name__ == "__main__":
    args = parse_args()
    asyncio.run(capnp.run(cmd_main(args)))
