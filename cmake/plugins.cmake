# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3

# Single list of plugins — add_subdirectory(), capnp schema collection, and
# compile-definition guards are all driven from this list.
set(TEST_RUNNER_PLUGINS
    weston_screenshooter
    agl_health
)

option(DISABLE_PLUGINS "Disable all plugins" OFF)

foreach(plugin IN LISTS TEST_RUNNER_PLUGINS)
  string(TOUPPER ${plugin} plugin_upper)
  option(ENABLE_PLUGIN_${plugin_upper} "Enable ${plugin} plugin" ON)
  if (DISABLE_PLUGINS)
    set(ENABLE_PLUGIN_${plugin_upper} OFF)
  endif ()
  if (ENABLE_PLUGIN_${plugin_upper})
    add_compile_definitions(ENABLE_PLUGIN_${plugin_upper})
  endif ()
endforeach()

option(ENABLE_PLUGIN_AGL_SCREENSHOOTER "Enable AGL Screenshooter Plugin" ON)
if (ENABLE_PLUGIN_AGL_SCREENSHOOTER)
    add_compile_definitions(ENABLE_PLUGIN_AGL_SCREENSHOOTER)
endif ()

