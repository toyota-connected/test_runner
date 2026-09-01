# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3

cmake_minimum_required(VERSION 3.10.2)
project(test_runner
        VERSION "0.1"
        DESCRIPTION "Linux Test Runner"
        LANGUAGES CXX
)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(third_party)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(plugins)
include(coverage)

set(CAPNPC_OUTPUT_DIR "${CMAKE_BINARY_DIR}")
file(MAKE_DIRECTORY ${CAPNPC_OUTPUT_DIR})

if(NOT BUILD_CAPNP)
  find_program(CAPNP_EXECUTABLE_HOST capnp REQUIRED)
  set(CAPNP_EXECUTABLE ${CAPNP_EXECUTABLE_HOST} CACHE STRING "Host capnp executable" FORCE)

  find_program(CAPNPC_CXX_EXECUTABLE_HOST capnpc-c++ REQUIRED)
  set(CAPNPC_CXX_EXECUTABLE ${CAPNPC_CXX_EXECUTABLE_HOST} CACHE STRING "Host capnp-c++ executable" FORCE)

  find_package(CapnProto REQUIRED)
endif()

if(ENABLE_PLUGIN_AGL_HEALTH)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(CURL REQUIRED libcurl)
endif()

# Recording: the recorder/ library, the TestRunner-Recorder tool, and the
# server's Recorder RPC methods. OFF drops all three. The methods stay in the
# schema and fall back to capnp's "unimplemented" bodies, so clients still work.
option(BUILD_RECORDER "Build input event recording support" ON)
if(BUILD_RECORDER)
  add_compile_definitions(ENABLE_RECORDER)
endif()

add_subdirectory(plugins)

# Core Test Runner capnp schemas
capnp_generate_cpp(CAPNP_SRCS CAPNP_HDRS
        capnp/test_runner.capnp
)

set(CAPNPC_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/capnp;-I${CMAKE_CURRENT_SOURCE_DIR}/plugins")
capnp_generate_cpp(TEST_RUNNER_PLUGIN_CAPNP_SRCS TEST_RUNNER_PLUGIN_CAPNP_HDRS
        ${TEST_RUNNER_PLUGIN_CAPNP_SCHEMAS}
)

# Server interface (composes core + plugin interfaces)
capnp_generate_cpp(CAPNP_SERVER_SRCS CAPNP_SERVER_HDRS
        capnp/test_runner_server.capnp
)

add_custom_target(capnp_generated
        DEPENDS
        ${CAPNP_SRCS} ${CAPNP_HDRS}
        ${TEST_RUNNER_PLUGIN_CAPNP_SRCS} ${TEST_RUNNER_PLUGIN_CAPNP_HDRS}
        ${CAPNP_SERVER_SRCS} ${CAPNP_SERVER_HDRS}
)

# ABS axis ranges — shared between server (uinput setup) and recorder (coordinate translation)
add_compile_definitions(VIRTUAL_MULTITOUCH_ABS_X_MAX=16000)
add_compile_definitions(VIRTUAL_MULTITOUCH_ABS_Y_MAX=9000)


# ── TestRunnerClient ──────────────────────────────────────────────────────────────────
add_library(TestRunnerClient SHARED
        client/TestRunnerClient.cpp
        ${CAPNP_SRCS}
        ${TEST_RUNNER_PLUGIN_CAPNP_SRCS}
        ${TEST_RUNNER_PLUGIN_CLIENT_SRCS}
        ${CAPNP_SERVER_SRCS}
)
add_dependencies(TestRunnerClient capnp_generated)
target_link_libraries(TestRunnerClient
        PRIVATE
        CapnProto::capnp
        CapnProto::capnp-rpc
        CapnProto::kj
        CapnProto::kj-async
        spdlog::spdlog
        ${TEST_RUNNER_PLUGIN_LIBS}
)
target_include_directories(TestRunnerClient
        PUBLIC
        client
        recorder
        plugins
        ${CMAKE_BINARY_DIR}
        ${TEST_RUNNER_PLUGIN_INC}
)
install(TARGETS TestRunnerClient)


if(BUILD_SERVER)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)

  if(BUILD_RECORDER)
  # ── TestRunnerRecorder ───────────────────────────────────────────────────────────────
  add_library(TestRunnerRecorder SHARED
          recorder/TestRunnerRecorder.cpp
  )
  target_link_libraries(TestRunnerRecorder
          PRIVATE
          udev
          spdlog::spdlog
          ${XKBCOMMON_LIBRARIES}
  )
  target_include_directories(TestRunnerRecorder
          PUBLIC
          recorder
          ${CMAKE_BINARY_DIR}
          PRIVATE
          ${XKBCOMMON_INCLUDE_DIRS}
  )
  if(ENABLE_LIBINPUT)
    target_link_libraries(TestRunnerRecorder PRIVATE input)
    add_compile_definitions(ENABLE_LIBINPUT)
  endif()
  install(TARGETS TestRunnerRecorder)
  endif()

  # ── TestRunnerServer ─────────────────────────────────────────────────────────────────
  add_library(TestRunnerServer SHARED
          server/TestRunnerServer.cpp
          ${TEST_RUNNER_PLUGIN_SRCS}
          ${CAPNP_SRCS}
          ${TEST_RUNNER_PLUGIN_CAPNP_SRCS}
          ${CAPNP_SERVER_SRCS}
  )
  add_dependencies(TestRunnerServer capnp_generated)
  target_include_directories(TestRunnerServer
          PUBLIC
          server
          # SyscallInterface.h is under recorder/, but the server needs it
          # with or without BUILD_RECORDER.
          recorder
          plugins
          ${TEST_RUNNER_PLUGIN_INC}
          ${CMAKE_BINARY_DIR}
          PRIVATE
          ${TEST_RUNNER_PLUGIN_SERVER_INC}
          ${XKBCOMMON_INCLUDE_DIRS}
  )
  target_link_libraries(TestRunnerServer
          PRIVATE
          udev
          rt
          CapnProto::capnp
          CapnProto::capnp-rpc
          CapnProto::kj
          CapnProto::kj-async
          spdlog::spdlog
          ${XKBCOMMON_LIBRARIES}
          ${TEST_RUNNER_PLUGIN_LIBS}
          ${TEST_RUNNER_PLUGIN_SERVER_LIBS}
  )
  if(BUILD_RECORDER)
    target_link_libraries(TestRunnerServer PRIVATE TestRunnerRecorder)
  endif()
  install(TARGETS TestRunnerServer)
endif()


if(BUILD_EXAMPLES)
  # ── TestRunner-Server executable ─────────────────────────────────────────────────────
  add_executable(TestRunner-Server
          server/main.cpp
  )
  add_dependencies(TestRunner-Server capnp_generated)
  target_include_directories(TestRunner-Server PRIVATE
          server
          recorder
          plugins
          ${CMAKE_BINARY_DIR}
  )
  target_link_libraries(TestRunner-Server
          PRIVATE
          TestRunnerServer
          udev
          CapnProto::capnp
          CapnProto::capnp-rpc
          CapnProto::kj
          CapnProto::kj-async
          spdlog::spdlog
  )
  install(TARGETS TestRunner-Server)

  # ── TestRunnerClientTest executable ──────────────────────────────────────────────────
  add_executable(TestRunnerClientTest
          client/client_test.cpp
  )
  add_dependencies(TestRunnerClientTest capnp_generated TestRunnerClient)
  target_include_directories(TestRunnerClientTest PRIVATE
          ${CMAKE_BINARY_DIR}
  )
  target_link_libraries(TestRunnerClientTest
          PRIVATE
          CapnProto::capnp
          CapnProto::capnp-rpc
          CapnProto::kj
          CapnProto::kj-async
          spdlog::spdlog
          TestRunnerClient
  )
  install(TARGETS TestRunnerClientTest)

  if(BUILD_RECORDER)
  # ── TestRunner-Recorder executable ───────────────────────────────────────────────────
  add_executable(TestRunner-Recorder
          recorder/recorder.cpp
  )
  target_link_libraries(TestRunner-Recorder
          PRIVATE
          TestRunnerRecorder
          udev
          spdlog::spdlog
  )
  if(ENABLE_LIBINPUT)
    target_link_libraries(TestRunner-Recorder PRIVATE input)
  endif()
  target_include_directories(TestRunner-Recorder PRIVATE
          ${CMAKE_BINARY_DIR}
  )
  install(TARGETS TestRunner-Recorder)
  endif()
endif()

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

if(BUILD_UNIT_TESTS)
  add_subdirectory(test/unit)
endif()