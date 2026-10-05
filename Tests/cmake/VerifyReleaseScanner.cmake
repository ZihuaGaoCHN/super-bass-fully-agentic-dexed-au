cmake_minimum_required(VERSION 3.24)

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(FIXTURE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/release-scanner-fixture")
file(REMOVE_RECURSE "${FIXTURE_ROOT}")
file(MAKE_DIRECTORY "${FIXTURE_ROOT}")

set(work_segment "work")
set(windows_runner_root "D:\\a")
set(secret_prefix "sk-agentic-release-")
file(WRITE "${FIXTURE_ROOT}/leak.log"
    "${secret_prefix}canary-0123456789\n"
    "mac=/Users/runner/${work_segment}/agentic-dexed/agentic-dexed/build/plugin.vst3\n"
    "linux=/home/runner/${work_segment}/agentic-dexed/agentic-dexed/build/plugin.vst3\n"
    "windows=${windows_runner_root}\\agentic-dexed\\agentic-dexed\\build\\plugin.vst3\n")

if(WIN32)
    execute_process(
        COMMAND pwsh -NoProfile -File "${REPOSITORY_ROOT}/scripts/scan-release.ps1"
            -ReleaseDirectory "${FIXTURE_ROOT}"
        RESULT_VARIABLE scan_result
        OUTPUT_VARIABLE scan_output
        ERROR_VARIABLE scan_error)
else()
    execute_process(
        COMMAND bash "${REPOSITORY_ROOT}/scripts/scan-release.sh"
            --release-directory "${FIXTURE_ROOT}"
        RESULT_VARIABLE scan_result
        OUTPUT_VARIABLE scan_output
        ERROR_VARIABLE scan_error)
endif()

file(REMOVE_RECURSE "${FIXTURE_ROOT}")
if(scan_result EQUAL 0)
    message(FATAL_ERROR "Release scanner accepted a fixture containing sensitive paths and a canary secret")
endif()
if(NOT "${scan_output}${scan_error}" MATCHES "sensitive content")
    message(FATAL_ERROR "Release scanner failed without identifying sensitive content: ${scan_output}${scan_error}")
endif()

message(STATUS "Super Bass Fully Agentic Dexed release scanner rejection verified")
