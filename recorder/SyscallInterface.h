// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#pragma once

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

// Interface over the POSIX/Linux syscalls used by TestRunnerRecorder and
// TestRunnerServer. The real implementation calls through; tests inject a mock.
struct SyscallInterface {
  virtual ~SyscallInterface() = default;

  // Filesystem
  virtual int open(const char* path, int flags) = 0;
  virtual int close(int fd) = 0;
  virtual DIR* opendir(const char* path) = 0;
  virtual struct dirent* readdir(DIR* dir) = 0;
  virtual int closedir(DIR* dir) = 0;

  // ioctl with pointer arg (EVIOCG*, UI_DEV_SETUP, UI_ABS_SETUP,
  // UI_DEV_DESTROY)
  virtual int ioctl(int fd, unsigned long request, void* arg) = 0;
  // ioctl with integer arg (UI_SET_EVBIT, UI_SET_KEYBIT, etc.)
  virtual int ioctl_int(int fd, unsigned long request, int arg) = 0;

  // I/O
  virtual ssize_t read(int fd, void* buf, size_t count) = 0;
  virtual ssize_t write(int fd, const void* buf, size_t count) = 0;

  // select
  virtual int select(int nfds,
                     fd_set* readfds,
                     fd_set* writefds,
                     fd_set* exceptfds,
                     struct timeval* timeout) = 0;

  // Shell execution
  virtual FILE* popen(const char* command, const char* type) = 0;
  virtual char* fgets(char* s, int size, FILE* stream) = 0;
  virtual int pclose(FILE* stream) = 0;

  // Timing
  virtual void usleep(useconds_t usec) = 0;
};

// Default implementation — passes every call straight to libc/kernel.
struct RealSyscalls : SyscallInterface {
  int open(const char* path, int flags) override { return ::open(path, flags); }
  int close(int fd) override { return ::close(fd); }
  DIR* opendir(const char* path) override { return ::opendir(path); }
  struct dirent* readdir(DIR* dir) override {
    return ::readdir(dir);
  }
  int closedir(DIR* dir) override { return ::closedir(dir); }
  int ioctl(int fd, unsigned long request, void* arg) override {
    return ::ioctl(fd, request, arg);
  }
  int ioctl_int(int fd, unsigned long request, int arg) override {
    return ::ioctl(fd, request, arg);
  }
  ssize_t read(int fd, void* buf, size_t count) override {
    return ::read(fd, buf, count);
  }
  ssize_t write(int fd, const void* buf, size_t count) override {
    return ::write(fd, buf, count);
  }
  int select(int nfds,
             fd_set* readfds,
             fd_set* writefds,
             fd_set* exceptfds,
             struct timeval* timeout) override {
    return ::select(nfds, readfds, writefds, exceptfds, timeout);
  }
  FILE* popen(const char* command, const char* type) override {
    return ::popen(command, type);
  }
  char* fgets(char* s, int size, FILE* stream) override {
    return ::fgets(s, size, stream);
  }
  int pclose(FILE* stream) override { return ::pclose(stream); }
  void usleep(useconds_t usec) override { ::usleep(usec); }
};
