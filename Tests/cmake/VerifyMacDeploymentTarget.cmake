cmake_minimum_required(VERSION 3.24)

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
file(READ "${REPOSITORY_ROOT}/CMakeLists.txt" root_cmake)

set(expected "set(CMAKE_OSX_DEPLOYMENT_TARGET \"11.0\" CACHE STRING")
string(FIND "${root_cmake}" "${expected}" deployment_position)
set(expected_environment
    "set(ENV{MACOSX_DEPLOYMENT_TARGET} \"\${CMAKE_OSX_DEPLOYMENT_TARGET}\")")
string(FIND "${root_cmake}" "${expected_environment}" environment_position)
string(FIND "${root_cmake}" "project(" project_position)

if(deployment_position EQUAL -1)
    message(FATAL_ERROR "Root CMake does not default the macOS deployment target to 11.0")
endif()
if(environment_position EQUAL -1)
    message(FATAL_ERROR "The macOS deployment target is not propagated to JUCE's bootstrap build")
endif()
if(project_position EQUAL -1 OR deployment_position GREATER project_position
   OR environment_position GREATER project_position)
    message(FATAL_ERROR "The macOS deployment target must be initialized before project()")
endif()
if(NOT root_cmake MATCHES "if\\(CMAKE_HOST_APPLE\\)")
    message(FATAL_ERROR "Pre-project macOS detection must use CMAKE_HOST_APPLE")
endif()

message(STATUS "Super Bass Fully Agentic Dexed macOS deployment target verified")
