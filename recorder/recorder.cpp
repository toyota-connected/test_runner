// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include <memory>
#include "TestRunnerRecorder.h"

#include <getopt.h>
#include <sys/wait.h>
#include <syslog.h>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "spdlog/cfg/env.h"
#include "spdlog/spdlog.h"

struct args {
  char* filename;
  int record_length;
  bool continuous;
};

static constexpr args argument_defaults = {
    .filename = (char*)"cmd_file_recorder",
    .record_length = 30,
    .continuous = false};

static void usage(const char* program_name) {
  printf("Usage: %s [OPTION]\n", program_name);
  printf(
      "\nOptions:\n"
      "  -f filename            Target recorded input file\n"
      "  -b {time in seconds}   Length of time to record"
      "  -c                     Continous recording, keeps last n seconds "
      "based on -b"
      "  -h  --help             show this help text and exit\n");
}

static args parse_args(const int argc, char* argv[]) {
  args args = argument_defaults;

  constexpr option long_options[] = {{"help", no_argument, nullptr, 'h'},
                                     {"filename", no_argument, nullptr, 'f'},
                                     {"buffer", no_argument, nullptr, 'b'},
                                     {"continuous", no_argument, nullptr, 'c'},
                                     {nullptr, 0, nullptr, 0}};

  int ch;
  while ((ch = getopt_long(argc, argv, "hb:cf:", long_options, nullptr)) > 0) {
    switch (ch) {
      case 'f':
        args.filename = optarg;
        break;
      case 'b':
        args.record_length = static_cast<int>(strtol(optarg, nullptr, 10));
        break;
      case 'c':
        args.continuous = true;
        break;
      case 'h':
        usage(argv[0]);
        exit(EXIT_SUCCESS);
      default:
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }
  }

  return args;
}

int main(const int argc, char** argv) {
  const args args = parse_args(argc, argv);
  spdlog::cfg::load_env_levels();

  auto recorder = std::make_unique<TestRunnerRecorder>(
      args.filename, args.record_length, args.continuous);

#if 0
  std::vector<in_device_s> devices = recorder->listDevices();
  std::vector<in_device_s> req_dev;
  for(in_device_s device : devices) {
    if(device.type == MOUSE) {
      req_dev.push_back(device);
    }
  }
  spdlog::debug("req_dev size: {}", req_dev.size());
  recorder->setDevicesToRecord(req_dev);
#endif
  recorder->Start();

  std::this_thread::sleep_for(std::chrono::seconds(10));

  recorder->Stop();
  // while(recorder->isActive()) {

  // }
  spdlog::debug("Ending program");
}
