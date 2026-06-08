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
	set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
	add_subdirectory(capnproto-1.4.0/c++ EXCLUDE_FROM_ALL)
endif()
