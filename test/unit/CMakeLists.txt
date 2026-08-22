# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
add_executable(test_runner_unit_tests
        test_recorder.cpp
        test_server.cpp
        test_client.cpp
        test_integration.cpp
        ServerThreadHelper.cpp
)

target_include_directories(test_runner_unit_tests PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/recorder
        ${CMAKE_SOURCE_DIR}/server
        ${CMAKE_SOURCE_DIR}/client
        ${CMAKE_SOURCE_DIR}/plugins
        ${CMAKE_SOURCE_DIR}/test/mocks
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/third_party/capnproto-c++-0.9.1/src
        ${CMAKE_SOURCE_DIR}/third_party/spdlog-1.12.0/include
)

target_compile_definitions(test_runner_unit_tests PRIVATE
        VIRTUAL_MULTITOUCH_ABS_X_MAX=16000
        VIRTUAL_MULTITOUCH_ABS_Y_MAX=9000
)

target_link_libraries(test_runner_unit_tests
        PRIVATE
        TestRunnerRecorder
        TestRunnerServer
        TestRunnerClient
        CapnProto::capnp-rpc
        CapnProto::kj-async
        gmock
        gtest_main
        gtest
        pthread
)

add_test(NAME test_runner_unit_tests COMMAND test_runner_unit_tests)
