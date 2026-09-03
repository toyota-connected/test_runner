# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
# Plugin: WaylandScreenshooter

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
        ${CMAKE_CURRENT_SOURCE_DIR}/WaylandScreenshooter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/WaylandCapture.cpp
        PARENT_SCOPE
)

set(TEST_RUNNER_PLUGIN_CLIENT_SRCS
        ${TEST_RUNNER_PLUGIN_CLIENT_SRCS}
        ${CMAKE_CURRENT_SOURCE_DIR}/WaylandScreenshooter_Client.cpp
        PARENT_SCOPE
)
