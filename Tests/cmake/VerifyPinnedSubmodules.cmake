cmake_minimum_required(VERSION 3.24)

find_package(Git REQUIRED)

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(EXPECTED_SUBMODULES
    "libs/JUCE|ae5144833e852815d61642af87c69b9db44984f7"
    "libs/MTS-ESP|803c3aab3d43dfaea430a1084ab31b606f5cd72c"
    "libs/clap-juce-extensions|4d454e5125da75a0e75d95615cbec26d2a09e2bf"
    "libs/surgesynthteam_tuningui|54f9a74cd55cdb33fb4d32d706067626857cfc75"
    "libs/tuning-library|3bbe9514816e1ae674c207b09e9f20eea4df372a"
    "libs/vst3sdk|56e4b2a644be164c5d324e8bc9de55b964b0f102"
)

foreach(entry IN LISTS EXPECTED_SUBMODULES)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 path)
    list(GET fields 1 expected_revision)

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse "HEAD:${path}"
        WORKING_DIRECTORY "${REPOSITORY_ROOT}"
        RESULT_VARIABLE gitlink_result
        OUTPUT_VARIABLE gitlink_revision
        ERROR_VARIABLE gitlink_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT gitlink_result EQUAL 0)
        message(FATAL_ERROR "Could not read gitlink ${path}: ${gitlink_error}")
    endif()

    if(NOT gitlink_revision STREQUAL expected_revision)
        message(FATAL_ERROR
            "Pinned gitlink mismatch for ${path}: expected ${expected_revision}, got ${gitlink_revision}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${REPOSITORY_ROOT}/${path}" rev-parse HEAD
        RESULT_VARIABLE checkout_result
        OUTPUT_VARIABLE checkout_revision
        ERROR_VARIABLE checkout_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT checkout_result EQUAL 0)
        message(FATAL_ERROR "Submodule ${path} is not initialized: ${checkout_error}")
    endif()

    if(NOT checkout_revision STREQUAL expected_revision)
        message(FATAL_ERROR
            "Checked-out submodule mismatch for ${path}: expected ${expected_revision}, got ${checkout_revision}")
    endif()
endforeach()

message(STATUS "Pinned Dexed submodules verified")
