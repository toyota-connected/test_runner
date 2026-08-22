# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
# Plugin: WestonScreenshooter

if(BUILD_SERVER)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)

  set(TEST_RUNNER_PLUGIN_SERVER_LIBS
          ${TEST_RUNNER_PLUGIN_SERVER_LIBS}
          ${WAYLAND_CLIENT_LIBRARIES}
          PARENT_SCOPE
  )
  set(TEST_RUNNER_PLUGIN_SERVER_INC
          ${TEST_RUNNER_PLUGIN_SERVER_INC}
          ${WAYLAND_CLIENT_INCLUDE_DIRS}
          PARENT_SCOPE
  )
endif()

set(TEST_RUNNER_PLUGIN_SRCS
        ${TEST_RUNNER_PLUGIN_SRCS}
        ${CMAKE_CURRENT_SOURCE_DIR}/WestonScreenshooter.cpp
        PARENT_SCOPE
)
set(TEST_RUNNER_PLUGIN_CAPNP_SCHEMAS
        ${TEST_RUNNER_PLUGIN_CAPNP_SCHEMAS}
        ${CMAKE_CURRENT_SOURCE_DIR}/weston_screenshooter.capnp
        PARENT_SCOPE
)

set(TEST_RUNNER_PLUGIN_CLIENT_SRCS
        ${TEST_RUNNER_PLUGIN_CLIENT_SRCS}
        ${CMAKE_CURRENT_SOURCE_DIR}/WestonScreenshooter_Client.cpp
        PARENT_SCOPE
)
