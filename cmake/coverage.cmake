# SPDX-FileCopyrightText: (c) 2026 Toyota Connected North America
# SPDX-License-Identifier: GPLv3
option(ENABLE_COVERAGE "Build with gcov/lcov code coverage instrumentation" OFF)

if(ENABLE_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "ENABLE_COVERAGE requires GCC or Clang")
  endif()

  add_compile_options(--coverage -O0 -fno-inline)
  add_link_options(--coverage)

  find_program(LCOV_EXECUTABLE lcov REQUIRED)
  find_program(GENHTML_EXECUTABLE genhtml REQUIRED)

  # Prefer a gcov that matches the compiler major version so that lcov can read
  # the .gcno files.  Falls back to plain gcov (works when compiler == default).
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    string(REGEX MATCH "^[0-9]+" _gcc_major "${CMAKE_CXX_COMPILER_VERSION}")
    find_program(GCOV_EXECUTABLE NAMES "gcov-${_gcc_major}" gcov REQUIRED)
  else()
    find_program(GCOV_EXECUTABLE NAMES "llvm-cov" gcov REQUIRED)
  endif()
  message(STATUS "Coverage gcov tool: ${GCOV_EXECUTABLE}")

  set(COVERAGE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/coverage_html")
  set(COVERAGE_INFO_FILE  "${CMAKE_BINARY_DIR}/coverage.info")
  set(COVERAGE_BASE_FILE  "${CMAKE_BINARY_DIR}/coverage_base.info")
  set(_lcov ${LCOV_EXECUTABLE} --gcov-tool "${GCOV_EXECUTABLE}")

  # Baseline capture (zero counters) — run once before any tests execute so
  # that source files with no test traffic still appear in the report.
  add_custom_target(coverage_baseline
    COMMAND ${_lcov}
            --capture --initial
            --directory "${CMAKE_BINARY_DIR}"
            --output-file "${COVERAGE_BASE_FILE}"
            --exclude "*/third_party/*"
            --exclude "*/test/*"
            --exclude "*/usr/*"
            --exclude "*capnp/*.capnp*"
            --exclude "*/plugins/*.capnp*"
            --quiet
    COMMENT "Capturing baseline coverage counters"
    VERBATIM
  )

  # Full report: reset → run tests → capture → combine with baseline → filter → HTML
  add_custom_target(coverage
    COMMAND ${_lcov} --zerocounters --directory "${CMAKE_BINARY_DIR}"
    COMMAND "${CMAKE_BINARY_DIR}/test/unit/test_runner_unit_tests"
    COMMAND ${_lcov}
            --capture
            --directory "${CMAKE_BINARY_DIR}"
            --output-file "${COVERAGE_INFO_FILE}"
            --exclude "*/third_party/*"
            --exclude "*/test/*"
            --exclude "*/usr/*"
            --exclude "*capnp/*.capnp*"
            --exclude "*/plugins/*.capnp*"
            --quiet
    COMMAND ${_lcov}
            --add-tracefile "${COVERAGE_BASE_FILE}"
            --add-tracefile "${COVERAGE_INFO_FILE}"
            --output-file  "${COVERAGE_INFO_FILE}"
            --quiet
    COMMAND ${GENHTML_EXECUTABLE}
            "${COVERAGE_INFO_FILE}"
            --output-directory "${COVERAGE_OUTPUT_DIR}"
            --title "test_runner coverage"
            --legend
            --quiet
    COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan
            "Coverage report: ${COVERAGE_OUTPUT_DIR}/index.html"
    DEPENDS test_runner_unit_tests coverage_baseline
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Generating lcov HTML coverage report"
    VERBATIM
  )
endif()
