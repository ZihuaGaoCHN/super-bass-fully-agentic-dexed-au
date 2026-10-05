cmake_minimum_required(VERSION 3.24)

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(DEFINED WORKFLOW_REVISION)
    find_package(Git REQUIRED)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" show "${WORKFLOW_REVISION}:.github/workflows/build.yml"
        WORKING_DIRECTORY "${REPOSITORY_ROOT}"
        RESULT_VARIABLE workflow_result
        OUTPUT_VARIABLE workflow
        ERROR_VARIABLE workflow_error
    )
    if(NOT workflow_result EQUAL 0)
        message(FATAL_ERROR "Could not read workflow at ${WORKFLOW_REVISION}: ${workflow_error}")
    endif()
else()
    file(READ "${REPOSITORY_ROOT}/.github/workflows/build.yml" workflow)
endif()

string(REPLACE "\r\n" "\n" workflow "${workflow}")

function(assert_workflow_contains expected)
    string(FIND "${workflow}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Build workflow is missing: ${expected}")
    endif()
endfunction()

assert_workflow_contains("  windows-x64:\n")
assert_workflow_contains("    runs-on: windows-2022\n")
assert_workflow_contains("  macos-x86_64:\n")
assert_workflow_contains("    runs-on: macos-15-intel\n")
assert_workflow_contains("          -DCMAKE_OSX_ARCHITECTURES=x86_64\n")
assert_workflow_contains("  macos-arm64:\n")
assert_workflow_contains("    runs-on: macos-15\n")
assert_workflow_contains("          -DCMAKE_OSX_ARCHITECTURES=arm64\n")
assert_workflow_contains("          tar -czf agentic-dexed-macos-x86_64.tar.gz\n")
assert_workflow_contains("          path: agentic-dexed-macos-x86_64.tar.gz\n")
assert_workflow_contains("          tar -czf agentic-dexed-macos-arm64.tar.gz\n")
assert_workflow_contains("          path: agentic-dexed-macos-arm64.tar.gz\n")

message(STATUS "Super Bass Fully Agentic Dexed build workflow verified")
