// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include "AglScreenshooter.h"
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
// agl_screenshooter protocol definitions
// ---------------------------------------------------------------------------

extern "C" {

extern const struct wl_interface agl_screenshooter_interface;

static const struct wl_message agl_screenshooter_requests[] = {
    {"take_shot", "oo", nullptr},
    {"destroy", "", nullptr},
};

static const struct wl_message agl_screenshooter_events[] = {
    {"done", "u", nullptr},
};

const struct wl_interface agl_screenshooter_interface = {
    "agl_screenshooter",          1, 2,
    agl_screenshooter_requests,   1, agl_screenshooter_events,
};

}  // extern "C"

struct agl_screenshooter;

enum agl_screenshooter_done_status {
  AGL_SCREENSHOOTER_DONE_STATUS_SUCCESS = 0,
  AGL_SCREENSHOOTER_DONE_STATUS_NO_MEMORY = 1,
  AGL_SCREENSHOOTER_DONE_STATUS_BAD_BUFFER = 2,
};

static inline struct agl_screenshooter* agl_screenshooter_bind(
    struct wl_registry* registry,
    uint32_t name) {
  return static_cast<struct agl_screenshooter*>(
      wl_registry_bind(registry, name, &agl_screenshooter_interface, 1));
}

static inline void agl_screenshooter_take_shot(
    struct agl_screenshooter* shooter,
    struct wl_output* output,
    struct wl_buffer* buffer) {
  wl_proxy_marshal(reinterpret_cast<struct wl_proxy*>(shooter),
                   0 /* take_shot */, output, buffer);
}

static inline void agl_screenshooter_destroy(
    struct agl_screenshooter* shooter) {
  wl_proxy_marshal(reinterpret_cast<struct wl_proxy*>(shooter),
                   1 /* destroy */);
  wl_proxy_destroy(reinterpret_cast<struct wl_proxy*>(shooter));
}

struct agl_screenshooter_listener {
  void (*done)(void* data, struct agl_screenshooter*, uint32_t status);
};

static inline int agl_screenshooter_add_listener(
    struct agl_screenshooter* shooter,
    const struct agl_screenshooter_listener* listener,
    void* data) {
  return wl_proxy_add_listener(
      reinterpret_cast<struct wl_proxy*>(shooter),
      reinterpret_cast<void (**)(void)>(
          const_cast<struct agl_screenshooter_listener*>(listener)),
      data);
}

// ---------------------------------------------------------------------------
// Capture state
// ---------------------------------------------------------------------------

struct AglOutputInfo {
  wl_output* output{nullptr};
  std::string model;
  int32_t width{0};
  int32_t height{0};
};

struct AglCaptureState {
  wl_display* display{nullptr};
  wl_registry* registry{nullptr};
  agl_screenshooter* shooter{nullptr};
  wl_shm* shm{nullptr};

  std::vector<AglOutputInfo> outputs;

  bool finished{false};
  uint32_t done_status{0};
  AglScreenshotResult result;
};

// ---------------------------------------------------------------------------
// wl_output listener
// ---------------------------------------------------------------------------

static AglOutputInfo* find_agl_output_info(AglCaptureState* s,
                                           wl_output* output) {
  for (auto& o : s->outputs)
    if (o.output == output)
      return &o;
  return nullptr;
}

static void on_agl_output_geometry(void* data,
                                   wl_output* output,
                                   int32_t,
                                   int32_t,
                                   int32_t,
                                   int32_t,
                                   int32_t,
                                   const char*,
                                   const char* model,
                                   int32_t) {
  auto* s = static_cast<AglCaptureState*>(data);
  if (auto* o = find_agl_output_info(s, output))
    o->model = model ? model : "";
}

static void on_agl_output_mode(void* data,
                               wl_output* output,
                               uint32_t flags,
                               int32_t width,
                               int32_t height,
                               int32_t) {
  if ((flags & WL_OUTPUT_MODE_CURRENT) != 0u) {
    auto* s = static_cast<AglCaptureState*>(data);
    if (auto* o = find_agl_output_info(s, output)) {
      o->width = width;
      o->height = height;
    }
  }
}

static void on_agl_output_done(void*, wl_output*) {}
static void on_agl_output_scale(void*, wl_output*, int32_t) {}

static const wl_output_listener agl_output_listener = {
    on_agl_output_geometry,
    on_agl_output_mode,
    on_agl_output_done,
    on_agl_output_scale,
};

// ---------------------------------------------------------------------------
// wl_registry listener
// ---------------------------------------------------------------------------

static void on_agl_registry_global(void* data,
                                   wl_registry* registry,
                                   uint32_t name,
                                   const char* interface,
                                   uint32_t version) {
  auto* s = static_cast<AglCaptureState*>(data);

  if (strcmp(interface, "agl_screenshooter") == 0) {
    s->shooter = agl_screenshooter_bind(registry, name);
  } else if (strcmp(interface, "wl_shm") == 0) {
    s->shm = static_cast<wl_shm*>(
        wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (strcmp(interface, "wl_output") == 0) {
    AglOutputInfo info;
    info.output = static_cast<wl_output*>(wl_registry_bind(
        registry, name, &wl_output_interface, version < 3 ? version : 3));
    s->outputs.push_back(info);
    wl_output_add_listener(s->outputs.back().output, &agl_output_listener, s);
  }
}

static void on_agl_registry_global_remove(void*, wl_registry*, uint32_t) {}

static const wl_registry_listener agl_registry_listener = {
    on_agl_registry_global,
    on_agl_registry_global_remove,
};

// ---------------------------------------------------------------------------
// agl_screenshooter done handler
// ---------------------------------------------------------------------------

static void on_agl_shoot_done(void* data,
                              agl_screenshooter*,
                              uint32_t status) {
  auto* s = static_cast<AglCaptureState*>(data);
  s->done_status = status;
  s->finished = true;
}

static const agl_screenshooter_listener agl_shooter_listener = {
    on_agl_shoot_done};

// ---------------------------------------------------------------------------
// PluginAglScreenshooter::capture()
// ---------------------------------------------------------------------------

AglScreenshotResult PluginAglScreenshooter::capture() {
  AglCaptureState s;

  s.display = wl_display_connect(nullptr);
  if (!s.display) {
    s.result.error = "wl_display_connect failed — is WAYLAND_DISPLAY set?";
    return s.result;
  }

  s.registry = wl_display_get_registry(s.display);
  wl_registry_add_listener(s.registry, &agl_registry_listener, &s);

  wl_display_roundtrip(s.display);
  wl_display_roundtrip(s.display);

  if (!s.shooter) {
    s.result.error = "agl_screenshooter global not found";
    goto cleanup;
  }
  if (!s.shm) {
    s.result.error = "wl_shm global not found";
    goto cleanup;
  }

  for (auto& o : s.outputs)
    SPDLOG_INFO("Output: model='{}' {}x{}", o.model, o.width, o.height);

  {
    AglOutputInfo* selected = nullptr;
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
    const int32_t stride = width * 4;
    const size_t size =
        static_cast<size_t>(stride) * static_cast<size_t>(height);

    int fd = static_cast<int>(
        syscall(SYS_memfd_create, "test-runner-agl-screenshot", 0));
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

    agl_screenshooter_add_listener(s.shooter, &agl_shooter_listener, &s);
    agl_screenshooter_take_shot(s.shooter, output, buffer);
    wl_display_flush(s.display);

    while (!s.finished && wl_display_dispatch(s.display) != -1) {
    }

    if (s.done_status != AGL_SCREENSHOOTER_DONE_STATUS_SUCCESS) {
      const char* status_str = "unknown error";
      if (s.done_status == AGL_SCREENSHOOTER_DONE_STATUS_NO_MEMORY)
        status_str = "no memory";
      else if (s.done_status == AGL_SCREENSHOOTER_DONE_STATUS_BAD_BUFFER)
        status_str = "bad buffer";
      s.result.error =
          std::string("agl_screenshooter failed: ") + status_str;
    } else {
      s.result.imageData.assign(static_cast<uint8_t*>(data),
                                static_cast<uint8_t*>(data) + size);
      s.result.width = static_cast<uint32_t>(width);
      s.result.height = static_cast<uint32_t>(height);
      SPDLOG_INFO("AGL screenshot captured: {}x{}", width, height);
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
    agl_screenshooter_destroy(s.shooter);
  if (s.registry)
    wl_registry_destroy(s.registry);
  if (s.display)
    wl_display_disconnect(s.display);

  if (!s.result.error.empty()) {
    SPDLOG_ERROR("AGL screenshot failed: {}", s.result.error);
  }
  return s.result;
}

// RPC Implementations

kj::Promise<void> PluginAglScreenshooter::aglTakeScreenshot(
    AglTakeScreenshotContext context) {
  AglScreenshotResult result = capture();
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
