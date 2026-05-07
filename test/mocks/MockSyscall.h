// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <gmock/gmock.h>

#include "recorder/SyscallInterface.h"

struct MockSyscall : SyscallInterface {
  MOCK_METHOD(int, open, (const char* path, int flags), (override));
  MOCK_METHOD(int, close, (int fd), (override));
  MOCK_METHOD(DIR*, opendir, (const char* path), (override));
  MOCK_METHOD(struct dirent*, readdir, (DIR* dir), (override));
  MOCK_METHOD(int, closedir, (DIR* dir), (override));
  MOCK_METHOD(int, ioctl, (int fd, unsigned long request, void* arg), (override));
  MOCK_METHOD(int, ioctl_int, (int fd, unsigned long request, int arg), (override));
  MOCK_METHOD(ssize_t, read, (int fd, void* buf, size_t count), (override));
  MOCK_METHOD(ssize_t, write, (int fd, const void* buf, size_t count), (override));
  MOCK_METHOD(int,
              select,
              (int nfds,
               fd_set* readfds,
               fd_set* writefds,
               fd_set* exceptfds,
               struct timeval* timeout),
              (override));
  MOCK_METHOD(FILE*, popen, (const char* command, const char* type), (override));
  MOCK_METHOD(char*, fgets, (char* s, int size, FILE* stream), (override));
  MOCK_METHOD(int, pclose, (FILE* stream), (override));
  MOCK_METHOD(void, usleep, (useconds_t usec), (override));
};
