// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include "WestonScreenshooter.h"
#include "TestRunnerServer.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <vector>

#include <wayland-client.h>

#include "spdlog/spdlog.h"

// ---------------------------------------------------------------------------
// Minimal weston_screenshooter protocol definitions
// ---------------------------------------------------------------------------

extern "C" {

extern const struct wl_interface weston_screenshooter_interface;

static const struct wl_message weston_screenshooter_requests[] = {
    {"shoot", "oo", nullptr},  // output, buffer -> (implicit done event)
};

static const struct wl_message weston_screenshooter_events[] = {
    {"done", "", nullptr},
};

const struct wl_interface weston_screenshooter_interface = {
    "weston_screenshooter",        1, 1,
    weston_screenshooter_requests, 1, weston_screenshooter_events,
};

}  // extern "C"

struct weston_screenshooter;

static inline struct weston_screenshooter* weston_screenshooter_bind(
    struct wl_registry* registry,
    uint32_t name) {
  return static_cast<struct weston_screenshooter*>(
      wl_registry_bind(registry, name, &weston_screenshooter_interface, 1));
}

static inline void weston_screenshooter_shoot(
    struct weston_screenshooter* shooter,
    struct wl_output* output,
    struct wl_buffer* buffer) {
  wl_proxy_marshal(reinterpret_cast<struct wl_proxy*>(shooter), 0 /* shoot */,
                   output, buffer);
}

static inline void weston_screenshooter_destroy(
    struct weston_screenshooter* shooter) {
  wl_proxy_destroy(reinterpret_cast<struct wl_proxy*>(shooter));
}

struct weston_screenshooter_listener {
  void (*done)(void* data, struct weston_screenshooter*);
};

static inline int weston_screenshooter_add_listener(
    struct weston_screenshooter* shooter,
    const struct weston_screenshooter_listener* listener,
    void* data) {
  return wl_proxy_add_listener(
      reinterpret_cast<struct wl_proxy*>(shooter),
      reinterpret_cast<void (**)(void)>(
          const_cast<struct weston_screenshooter_listener*>(listener)),
      data);
}

// ---------------------------------------------------------------------------
// Capture state
// ---------------------------------------------------------------------------

struct OutputInfo {
  wl_output* output{nullptr};
  std::string model;  // connector name from geometry event (e.g. "DP-1")
  int32_t width{0};
  int32_t height{0};
};

struct CaptureState {
  wl_display* display{nullptr};
  wl_registry* registry{nullptr};
  weston_screenshooter* shooter{nullptr};
  wl_shm* shm{nullptr};

  std::vector<OutputInfo> outputs;  // all bound outputs

  bool finished{false};
  ScreenshotResult result;
};

// ---------------------------------------------------------------------------
// wl_output listener — collect model name and dimensions per output
// ---------------------------------------------------------------------------

static OutputInfo* find_output_info(CaptureState* s, wl_output* output) {
  for (auto& o : s->outputs)
    if (o.output == output)
      return &o;
  return nullptr;
}

static void on_output_geometry(void* data,
                               wl_output* output,
                               int32_t,
                               int32_t,
                               int32_t,
                               int32_t,
                               int32_t,
                               const char*,
                               const char* model,
                               int32_t) {
  auto* s = static_cast<CaptureState*>(data);
  if (auto* o = find_output_info(s, output))
    o->model = model ? model : "";
}

static void on_output_mode(void* data,
                           wl_output* output,
                           uint32_t flags,
                           int32_t width,
                           int32_t height,
                           int32_t) {
  if ((flags & WL_OUTPUT_MODE_CURRENT) != 0u) {
    auto* s = static_cast<CaptureState*>(data);
    if (auto* o = find_output_info(s, output)) {
      o->width = width;
      o->height = height;
    }
  }
}

static void on_output_done(void*, wl_output*) {}
static void on_output_scale(void*, wl_output*, int32_t) {}

static const wl_output_listener output_listener = {
    on_output_geometry,
    on_output_mode,
    on_output_done,
    on_output_scale,
};

// ---------------------------------------------------------------------------
// wl_registry listener
// ---------------------------------------------------------------------------

static void on_registry_global(void* data,
                               wl_registry* registry,
                               uint32_t name,
                               const char* interface,
                               uint32_t version) {
  auto* s = static_cast<CaptureState*>(data);

  if (strcmp(interface, "weston_screenshooter") == 0) {
    s->shooter = weston_screenshooter_bind(registry, name);
  } else if (strcmp(interface, "wl_shm") == 0) {
    s->shm = static_cast<wl_shm*>(
        wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (strcmp(interface, "wl_output") == 0) {
    OutputInfo info;
    info.output = static_cast<wl_output*>(wl_registry_bind(
        registry, name, &wl_output_interface, version < 3 ? version : 3));
    s->outputs.push_back(info);
    wl_output_add_listener(s->outputs.back().output, &output_listener, s);
  }
}

static void on_registry_global_remove(void*, wl_registry*, uint32_t) {}

static const wl_registry_listener registry_listener = {
    on_registry_global,
    on_registry_global_remove,
};

// ---------------------------------------------------------------------------
// weston_screenshooter done handler
// ---------------------------------------------------------------------------

static void on_shoot_done(void* data, weston_screenshooter*) {
  auto* s = static_cast<CaptureState*>(data);
  s->finished = true;
}

static const weston_screenshooter_listener shooter_listener = {on_shoot_done};

// ---------------------------------------------------------------------------
// WestonScreenshooterImpl::capture()
// ---------------------------------------------------------------------------

ScreenshotResult PluginWestonScreenshooter::capture() {
  CaptureState s;

  s.display = wl_display_connect(nullptr);
  if (!s.display) {
    s.result.error = "wl_display_connect failed — is WAYLAND_DISPLAY set?";
    return s.result;
  }

  s.registry = wl_display_get_registry(s.display);
  wl_registry_add_listener(s.registry, &registry_listener, &s);

  wl_display_roundtrip(s.display);  // bind globals
  wl_display_roundtrip(s.display);  // flush wl_output mode/done events

  if (!s.shooter) {
    s.result.error = "weston_screenshooter global not found";
    goto cleanup;
  }
  if (!s.shm) {
    s.result.error = "wl_shm global not found";
    goto cleanup;
  }

  for (auto& o : s.outputs)
    SPDLOG_INFO("Output: model='{}' {}x{}", o.model, o.width, o.height);

  {
    // Select DP-1; fall back to first output if not found
    OutputInfo* selected = nullptr;
    for (auto& o : s.outputs)
      if (o.model == "DP-1") {
        selected = &o;
        break;
      }
    if (!selected && !s.outputs.empty())
      selected = &s.outputs[0];

    if (!selected) {
      s.result.error = "no wl_output found";
      goto cleanup;
    }
    if (selected->width <= 0 || selected->height <= 0) {
      s.result.error = "wl_output dimensions not received";
      goto cleanup;
    }

    SPDLOG_INFO("Capturing output '{}'", selected->model);
    const int32_t width = selected->width;
    const int32_t height = selected->height;
    wl_output* output = selected->output;
    const int32_t stride = width * 4;  // XRGB8888 — 4 bytes per pixel
    const size_t size =
        static_cast<size_t>(stride) * static_cast<size_t>(height);

    // Create anonymous shared memory suitable for wl_shm
    int fd = static_cast<int>(
        syscall(SYS_memfd_create, "test-runner-screenshot", 0));
    if (fd < 0) {
      s.result.error = "memfd_create failed";
      goto cleanup;
    }

    if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
      s.result.error = "ftruncate failed";
      close(fd);
      goto cleanup;
    }

    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
      s.result.error = "mmap failed";
      close(fd);
      goto cleanup;
    }

    wl_shm_pool* pool =
        wl_shm_create_pool(s.shm, fd, static_cast<int32_t>(size));
    wl_buffer* buffer = wl_shm_pool_create_buffer(
        pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    weston_screenshooter_add_listener(s.shooter, &shooter_listener, &s);
    weston_screenshooter_shoot(s.shooter, output, buffer);
    wl_display_flush(s.display);

    while (!s.finished && wl_display_dispatch(s.display) != -1) {
    }

    {
      s.result.imageData.assign(static_cast<uint8_t*>(data),
                                static_cast<uint8_t*>(data) + size);
      s.result.width = static_cast<uint32_t>(width);
      s.result.height = static_cast<uint32_t>(height);
      SPDLOG_INFO("Screenshot captured: {}x{}", width, height);
    }
    if (buffer != nullptr) {
      wl_buffer_destroy(buffer);
    }
    munmap(data, size);
  }

cleanup:
  for (auto& o : s.outputs)
    if (o.output)
      wl_output_destroy(o.output);
  if (s.shm)
    wl_shm_destroy(s.shm);
  if (s.shooter)
    weston_screenshooter_destroy(s.shooter);
  if (s.registry)
    wl_registry_destroy(s.registry);
  if (s.display)
    wl_display_disconnect(s.display);

  if (!s.result.error.empty()) {
    SPDLOG_ERROR("Screenshot failed: {}", s.result.error);
  }
  return s.result;
}

// RPC Implementations

kj::Promise<void> PluginWestonScreenshooter::takeScreenshot(
    TakeScreenshotContext context) {
  ScreenshotResult result = capture();
  if (!result.error.empty()) {
    return kj::Exception(kj::Exception::Type::FAILED, __FILE__, __LINE__,
                         kj::str(result.error));
  }
  auto ret = context.getResults();
  ret.setImageData(
      kj::arrayPtr(result.imageData.data(), result.imageData.size()));
  ret.setWidth(result.width);
  ret.setHeight(result.height);
  return kj::READY_NOW;
}