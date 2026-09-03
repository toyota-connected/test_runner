// SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
// SPDX-License-Identifier: GPLv3
#include "WaylandCapture.h"

#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstring>

#include "spdlog/spdlog.h"

// ===========================================================================
// Shared helpers
// ===========================================================================

WaylandOutputInfo* find_output(WaylandCaptureBase* s, wl_output* output) {
  for (auto& o : s->outputs)
    if (o.output == output)
      return &o;
  return nullptr;
}

WaylandOutputInfo* select_output(WaylandCaptureBase* s,
                                 const std::string& preferred) {
  if (!preferred.empty()) {
    for (auto& o : s->outputs)
      if (o.model == preferred)
        return &o;
  }
  if (!s->outputs.empty())
    return &s->outputs[0];
  return nullptr;
}

static void on_output_geometry(void* data,
                               wl_output* output,
                               int32_t, int32_t, int32_t, int32_t, int32_t,
                               const char*,
                               const char* model,
                               int32_t) {
  auto* s = static_cast<WaylandCaptureBase*>(data);
  if (auto* o = find_output(s, output))
    o->model = model ? model : "";
}

static void on_output_mode(void* data,
                           wl_output* output,
                           uint32_t flags,
                           int32_t width,
                           int32_t height,
                           int32_t) {
  if ((flags & WL_OUTPUT_MODE_CURRENT) != 0u) {
    auto* s = static_cast<WaylandCaptureBase*>(data);
    if (auto* o = find_output(s, output)) {
      o->width = width;
      o->height = height;
    }
  }
}

static void on_output_done(void*, wl_output*) {}
static void on_output_scale(void*, wl_output*, int32_t) {}

const wl_output_listener wayland_output_listener = {
    on_output_geometry,
    on_output_mode,
    on_output_done,
    on_output_scale,
};

bool bind_common_globals(WaylandCaptureBase* s, wl_registry* registry,
                         uint32_t name, const char* interface,
                         uint32_t version) {
  if (strcmp(interface, "wl_shm") == 0) {
    s->shm = static_cast<wl_shm*>(
        wl_registry_bind(registry, name, &wl_shm_interface, 1));
    return true;
  }
  if (strcmp(interface, "wl_output") == 0) {
    WaylandOutputInfo info;
    info.output = static_cast<wl_output*>(wl_registry_bind(
        registry, name, &wl_output_interface, version < 3 ? version : 3));
    s->outputs.push_back(info);
    wl_output_add_listener(s->outputs.back().output,
                           &wayland_output_listener, s);
    return true;
  }
  return false;
}

WaylandShmBuffer create_shm_buffer(WaylandCaptureBase* s,
                                   int32_t width, int32_t height) {
  const int32_t stride = width * 4;  // XRGB8888 — 4 bytes per pixel
  const size_t size =
      static_cast<size_t>(stride) * static_cast<size_t>(height);

  int fd = static_cast<int>(
      syscall(SYS_memfd_create, "test-runner-screenshot", 0));
  if (fd < 0) {
    s->result.error = "memfd_create failed";
    return {};
  }

  if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
    s->result.error = "ftruncate failed";
    close(fd);
    return {};
  }

  void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    s->result.error = "mmap failed";
    close(fd);
    return {};
  }

  wl_shm_pool* pool =
      wl_shm_create_pool(s->shm, fd, static_cast<int32_t>(size));
  wl_buffer* buffer = wl_shm_pool_create_buffer(
      pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);

  return {buffer, data, size};
}

void destroy_shm_buffer(WaylandShmBuffer& buf) {
  if (buf.buffer)
    wl_buffer_destroy(buf.buffer);
  if (buf.data)
    munmap(buf.data, buf.size);
}

void cleanup_wayland_base(WaylandCaptureBase* s) {
  for (auto& o : s->outputs)
    if (o.output)
      wl_output_destroy(o.output);
  if (s->shm)
    wl_shm_destroy(s->shm);
  if (s->registry)
    wl_registry_destroy(s->registry);
  if (s->display)
    wl_display_disconnect(s->display);
}

// ===========================================================================
// Protocol definitions — agl_screenshooter (verified against agl-screenshooter.xml)
// ===========================================================================

extern "C" {

static const struct wl_message agl_screenshooter_requests[] = {
    {"take_shot", "oo", nullptr},
    {"destroy", "", nullptr},
};

static const struct wl_message agl_screenshooter_events[] = {
    {"done", "u", nullptr},
};

static const struct wl_interface agl_screenshooter_interface = {
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

// ===========================================================================
// Protocol definitions — weston_screenshooter
// ===========================================================================

extern "C" {

static const struct wl_message weston_screenshooter_requests[] = {
    {"shoot", "oo", nullptr},
};

static const struct wl_message weston_screenshooter_events[] = {
    {"done", "", nullptr},
};

static const struct wl_interface weston_screenshooter_interface = {
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

// ===========================================================================
// Auto-detecting capture
// ===========================================================================

struct AutoCaptureState : WaylandCaptureBase {
  agl_screenshooter* agl_shooter{nullptr};
  weston_screenshooter* weston_shooter{nullptr};
  uint32_t agl_done_status{AGL_SCREENSHOOTER_DONE_STATUS_SUCCESS};
};

static void on_auto_registry_global(void* data,
                                    wl_registry* registry,
                                    uint32_t name,
                                    const char* interface,
                                    uint32_t version) {
  auto* s = static_cast<AutoCaptureState*>(data);
  if (bind_common_globals(s, registry, name, interface, version))
    return;
  if (strcmp(interface, "agl_screenshooter") == 0)
    s->agl_shooter = agl_screenshooter_bind(registry, name);
  else if (strcmp(interface, "weston_screenshooter") == 0)
    s->weston_shooter = weston_screenshooter_bind(registry, name);
}

static void on_auto_registry_global_remove(void*, wl_registry*, uint32_t) {}

static const wl_registry_listener auto_registry_listener = {
    on_auto_registry_global,
    on_auto_registry_global_remove,
};

static void on_agl_shoot_done(void* data,
                              agl_screenshooter*,
                              uint32_t status) {
  auto* s = static_cast<AutoCaptureState*>(data);
  s->agl_done_status = status;
  s->finished = true;
}

static const agl_screenshooter_listener agl_shooter_listener = {
    on_agl_shoot_done};

static void on_weston_shoot_done(void* data, weston_screenshooter*) {
  auto* s = static_cast<AutoCaptureState*>(data);
  s->finished = true;
}

static const weston_screenshooter_listener weston_shooter_listener = {
    on_weston_shoot_done};

ScreenshotResult wayland_capture_screenshot(const std::string& output_name) {
  AutoCaptureState s;

  s.display = wl_display_connect(nullptr);
  if (!s.display) {
    s.result.error = "wl_display_connect failed — is WAYLAND_DISPLAY set?";
    return s.result;
  }

  s.registry = wl_display_get_registry(s.display);
  wl_registry_add_listener(s.registry, &auto_registry_listener, &s);

  wl_display_roundtrip(s.display);  // bind globals
  wl_display_roundtrip(s.display);  // receive wl_output mode/geometry events

  const bool use_agl = s.agl_shooter != nullptr;

  if (!s.agl_shooter && !s.weston_shooter) {
    s.result.error =
        "no screenshooter global found "
        "(tried agl_screenshooter and weston_screenshooter)";
    goto cleanup;
  }
  if (!s.shm) {
    s.result.error = "wl_shm global not found";
    goto cleanup;
  }

  if (use_agl)
    SPDLOG_INFO("Using agl_screenshooter protocol");
  else
    SPDLOG_INFO("Using weston_screenshooter protocol");

  for (auto& o : s.outputs)
    SPDLOG_INFO("Output: model='{}' {}x{}", o.model, o.width, o.height);

  {
    auto* selected = select_output(&s, output_name);
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

    auto buf = create_shm_buffer(&s, width, height);
    if (!buf.buffer)
      goto cleanup;

    if (use_agl) {
      agl_screenshooter_add_listener(s.agl_shooter,
                                     &agl_shooter_listener, &s);
      agl_screenshooter_take_shot(s.agl_shooter, selected->output, buf.buffer);
    } else {
      weston_screenshooter_add_listener(s.weston_shooter,
                                        &weston_shooter_listener, &s);
      weston_screenshooter_shoot(s.weston_shooter, selected->output,
                                 buf.buffer);
    }
    wl_display_flush(s.display);

    while (!s.finished && wl_display_dispatch(s.display) != -1) {
    }

    if (!s.finished) {
      s.result.error = "compositor disconnected before screenshot completed";
    } else if (use_agl &&
               s.agl_done_status != AGL_SCREENSHOOTER_DONE_STATUS_SUCCESS) {
      const char* status_str = "unknown error";
      if (s.agl_done_status == AGL_SCREENSHOOTER_DONE_STATUS_NO_MEMORY)
        status_str = "no memory";
      else if (s.agl_done_status == AGL_SCREENSHOOTER_DONE_STATUS_BAD_BUFFER)
        status_str = "bad buffer";
      s.result.error =
          std::string("agl_screenshooter failed: ") + status_str;
    } else {
      s.result.imageData.assign(static_cast<uint8_t*>(buf.data),
                                static_cast<uint8_t*>(buf.data) + buf.size);
      s.result.width = static_cast<uint32_t>(width);
      s.result.height = static_cast<uint32_t>(height);
      SPDLOG_INFO("Screenshot captured: {}x{}", width, height);
    }

    destroy_shm_buffer(buf);
  }

cleanup:
  if (s.agl_shooter)
    agl_screenshooter_destroy(s.agl_shooter);
  if (s.weston_shooter)
    weston_screenshooter_destroy(s.weston_shooter);
  cleanup_wayland_base(&s);

  if (!s.result.error.empty())
    SPDLOG_ERROR("Screenshot failed: {}", s.result.error);
  return s.result;
}
