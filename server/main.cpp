// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include <getopt.h>
#include <unistd.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include <kj/async-io.h>

#include "TestRunnerServer.h"

#include "spdlog/cfg/env.h"
#include "spdlog/spdlog.h"

static constexpr const char* DEFAULT_LISTEN = "*:4004";

static int signal_pipe[2];

static void signal_handler(int) {
  char byte = 1;
  // write() is async-signal-safe; ignore return value in signal context
  (void)write(signal_pipe[1], &byte, sizeof(byte));
}

static void usage(const char* program_name) {
  printf("Usage: %s [OPTION]\n", program_name);
  printf(
      "\nOptions:\n"
      "  -l hostname/ip       listen address (default: *:4004)\n"
      "  -L layout            XKB keyboard layout (default: XKB_DEFAULT_LAYOUT "
      "env or system default)\n"
      "  -V variant           XKB layout variant\n"
      "  -M model             XKB keyboard model\n"
      "  -h  --help           show this help text and exit\n");
}

int main(int argc, char** argv) {
  spdlog::cfg::load_env_levels();

  const char* listen_addr = DEFAULT_LISTEN;
  XkbConfig xkb_cfg;

  constexpr option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"xkb-layout", required_argument, nullptr, 'L'},
      {"xkb-variant", required_argument, nullptr, 'V'},
      {"xkb-model", required_argument, nullptr, 'M'},
      {nullptr, 0, nullptr, 0}};
  int ch;
  while ((ch = getopt_long(argc, argv, "hl:L:V:M:", long_options, nullptr)) >
         0) {
    switch (ch) {
      case 'l':
        listen_addr = optarg;
        break;
      case 'L':
        xkb_cfg.layout = optarg;
        break;
      case 'V':
        xkb_cfg.variant = optarg;
        break;
      case 'M':
        xkb_cfg.model = optarg;
        break;
      case 'h':
        usage(argv[0]);
        return EXIT_SUCCESS;
      default:
        usage(argv[0]);
        return EXIT_FAILURE;
    }
  }

  if (pipe(signal_pipe) < 0) {
    SPDLOG_ERROR("Failed to create signal pipe");
    return EXIT_FAILURE;
  }
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

  auto server = std::make_unique<capnp::EzRpcServer>(
      kj::heap<TestRunnerServer>(nullptr, true, true, xkb_cfg), listen_addr);
  SPDLOG_INFO("Test Runner server listening on {}", listen_addr);

  auto& waitScope = server->getWaitScope();

  // Block until a signal byte arrives on the pipe, then fall through to
  // cleanup. signalIn must be destroyed before server since it holds a
  // registration in server's event loop.
  {
    auto signalIn = server->getLowLevelIoProvider().wrapInputFd(signal_pipe[0]);
    char buf[1];
    signalIn->read(buf, 1, 1).wait(waitScope);
  }

  SPDLOG_INFO("Shutting down...");
  server.reset();  // ~TestRunnerServer() destroys uinput devices

  close(signal_pipe[1]);
  return EXIT_SUCCESS;
}
