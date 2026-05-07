# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
add_executable(kj_client
        kj_client.cpp
        ${CMAKE_BINARY_DIR}/capnp/test_runner.capnp.c++
)
add_dependencies(kj_client capnp_generated_files_v1)

target_include_directories(kj_client PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(kj_client
        PRIVATE
        capnp
        capnp-rpc
        kj
        kj-async
        spdlog::spdlog
)

add_executable(kj_server
        kj_server.cpp
        ${CMAKE_BINARY_DIR}/capnp/test_runner.capnp.c++
)
add_dependencies(kj_server capnp_generated_files_v1)

target_include_directories(kj_server PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(kj_server
        PRIVATE
        capnp
        capnp-rpc
        kj
        kj-async
        spdlog::spdlog
)