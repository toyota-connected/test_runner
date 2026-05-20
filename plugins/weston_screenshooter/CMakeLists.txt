# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
# Plugin: WestonScreenshooter

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
