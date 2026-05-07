# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3

#
# spdlog
#
set(SPDLOG_NO_EXCEPTIONS ON)
set(SPDLOG_NO_THREAD_ID ON)
set(SPDLOG_BUILD_PIC ON)
add_subdirectory(spdlog-1.12.0 EXCLUDE_FROM_ALL)

#
# capnproto
if(BUILD_CAPNP)
	execute_process(COMMAND ./configure
				WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/capnproto-c++-0.9.1)

	set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
	add_subdirectory(capnproto-c++-0.9.1 EXCLUDE_FROM_ALL)
endif()
