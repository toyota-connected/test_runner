# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
set(TEST_RUNNER_PLUGIN_SRCS "")
set(TEST_RUNNER_PLUGIN_INC "")
set(TEST_RUNNER_PLUGIN_CLIENT_SRCS "")
set(TEST_RUNNER_PLUGIN_LIBS "")
set(TEST_RUNNER_PLUGIN_SERVER_LIBS "")
set(TEST_RUNNER_PLUGIN_SERVER_INC "")

if (ENABLE_PLUGIN_WESTON_SCREENSHOOTER)
	add_subdirectory(weston_screenshooter)
endif ()

if (ENABLE_PLUGIN_AGL_HEALTH)
	add_subdirectory(agl_health)
endif ()

# Capnp schemas are always compiled so the composite Plugins interface stays
# intact.  Disabled plugins simply fall back to capnp's default
# "unimplemented" response (same pattern as ENABLE_RECORDER).
set(TEST_RUNNER_PLUGIN_CAPNP_SCHEMAS
	${CMAKE_CURRENT_SOURCE_DIR}/weston_screenshooter/weston_screenshooter.capnp
	${CMAKE_CURRENT_SOURCE_DIR}/agl_health/agl_health.capnp
	${CMAKE_CURRENT_SOURCE_DIR}/plugins.capnp
	PARENT_SCOPE
)

set(TEST_RUNNER_PLUGIN_SRCS
	${TEST_RUNNER_PLUGIN_SRCS}
	${CMAKE_CURRENT_SOURCE_DIR}/plugins.cpp
	PARENT_SCOPE
)
set(TEST_RUNNER_PLUGIN_CLIENT_SRCS   ${TEST_RUNNER_PLUGIN_CLIENT_SRCS}  PARENT_SCOPE)
set(TEST_RUNNER_PLUGIN_INC           ${TEST_RUNNER_PLUGIN_INC}          PARENT_SCOPE)
set(TEST_RUNNER_PLUGIN_LIBS          ${TEST_RUNNER_PLUGIN_LIBS}         PARENT_SCOPE)
set(TEST_RUNNER_PLUGIN_SERVER_LIBS   ${TEST_RUNNER_PLUGIN_SERVER_LIBS}  PARENT_SCOPE)
set(TEST_RUNNER_PLUGIN_SERVER_INC    ${TEST_RUNNER_PLUGIN_SERVER_INC}   PARENT_SCOPE)
