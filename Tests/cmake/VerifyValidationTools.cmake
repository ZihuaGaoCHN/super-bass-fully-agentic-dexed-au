cmake_minimum_required(VERSION 3.22)

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(VERSION_FILE "${REPOSITORY_ROOT}/tools/pluginval/VERSION")
if(NOT EXISTS "${VERSION_FILE}")
    message(FATAL_ERROR "Missing pluginval version pin: ${VERSION_FILE}")
endif()

file(READ "${VERSION_FILE}" PLUGINVAL_VERSION)
string(STRIP "${PLUGINVAL_VERSION}" PLUGINVAL_VERSION)
if(NOT PLUGINVAL_VERSION STREQUAL "v1.0.4")
    message(FATAL_ERROR "pluginval must be pinned to v1.0.4, got '${PLUGINVAL_VERSION}'")
endif()

foreach(SCRIPT IN ITEMS scripts/download-pluginval.ps1 scripts/download-pluginval.sh)
    set(SCRIPT_PATH "${REPOSITORY_ROOT}/${SCRIPT}")
    if(NOT EXISTS "${SCRIPT_PATH}")
        message(FATAL_ERROR "Missing pluginval download script: ${SCRIPT}")
    endif()

    file(READ "${SCRIPT_PATH}" CONTENTS)
    if(NOT CONTENTS MATCHES "/releases/download/v1\\.0\\.4/")
        message(FATAL_ERROR "${SCRIPT} does not use the pinned v1.0.4 release URL")
    endif()
    string(TOLOWER "${CONTENTS}" CONTENTS_LOWER)
    if(CONTENTS_LOWER MATCHES "/latest/|latest_release")
        message(FATAL_ERROR "${SCRIPT} contains an unpinned latest-release URL")
    endif()
endforeach()

foreach(SCRIPT IN ITEMS scripts/validate-plugin.ps1 scripts/validate-plugin.sh)
    if(NOT EXISTS "${REPOSITORY_ROOT}/${SCRIPT}")
        message(FATAL_ERROR "Missing pluginval validation script: ${SCRIPT}")
    endif()
endforeach()

message(STATUS "pluginval validation tools are pinned to ${PLUGINVAL_VERSION}")
