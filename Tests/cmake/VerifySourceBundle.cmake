cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED SOURCE_ARCHIVE OR SOURCE_ARCHIVE STREQUAL "")
    message(FATAL_ERROR "SOURCE_ARCHIVE is required")
endif()
get_filename_component(SOURCE_ARCHIVE "${SOURCE_ARCHIVE}" ABSOLUTE)
if(NOT EXISTS "${SOURCE_ARCHIVE}")
    message(FATAL_ERROR "Source archive does not exist: ${SOURCE_ARCHIVE}")
endif()

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(DEFINED ENV{TEMP} AND NOT "$ENV{TEMP}" STREQUAL "")
    set(TEMPORARY_BASE "$ENV{TEMP}")
elseif(DEFINED ENV{TMPDIR} AND NOT "$ENV{TMPDIR}" STREQUAL "")
    set(TEMPORARY_BASE "$ENV{TMPDIR}")
else()
    set(TEMPORARY_BASE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef RANDOM_SUFFIX)
set(EXTRACT_ROOT "${TEMPORARY_BASE}/agentic-dexed-source-${RANDOM_SUFFIX}")
file(MAKE_DIRECTORY "${EXTRACT_ROOT}")
file(ARCHIVE_EXTRACT INPUT "${SOURCE_ARCHIVE}" DESTINATION "${EXTRACT_ROOT}")

file(GLOB EXTRACTED_TOP_LEVEL LIST_DIRECTORIES true "${EXTRACT_ROOT}/*")
list(FILTER EXTRACTED_TOP_LEVEL INCLUDE REGEX "Super-Bass-Fully-Agentic-Dexed-.*-source$")
list(LENGTH EXTRACTED_TOP_LEVEL ROOT_COUNT)
if(NOT ROOT_COUNT EQUAL 1)
    file(REMOVE_RECURSE "${EXTRACT_ROOT}")
    message(FATAL_ERROR "Source archive must contain exactly one versioned root")
endif()
list(GET EXTRACTED_TOP_LEVEL 0 SOURCE_ROOT)

set(REQUIRED_FILES
    CMakeLists.txt
    Source/PluginProcessor.cpp
    LICENSE
    THIRD_PARTY_NOTICES.md
    README.md
    .gitmodules
    source-manifest.json
    Documentation/BuildingAgenticDexed.md
    docs/superpowers/specs/2026-10-01-agentic-dexed-design.md
    docs/superpowers/plans/2026-10-01-agentic-dexed-phase-5-release.md
    libs/JUCE/CMakeLists.txt
    libs/MTS-ESP/LICENSE
    libs/clap-juce-extensions/CMakeLists.txt
    libs/surgesynthteam_tuningui/surgesynthteam_tuningui.cpp
    libs/tuning-library/CMakeLists.txt
    libs/vst3sdk/CMakeLists.txt)
foreach(REQUIRED IN LISTS REQUIRED_FILES)
    if(NOT EXISTS "${SOURCE_ROOT}/${REQUIRED}")
        file(REMOVE_RECURSE "${EXTRACT_ROOT}")
        message(FATAL_ERROR "Source archive is missing ${REQUIRED}")
    endif()
endforeach()

file(GLOB_RECURSE SHELL_FILES LIST_DIRECTORIES false "${SOURCE_ROOT}/*.sh")
foreach(SHELL_FILE IN LISTS SHELL_FILES)
    file(READ "${SHELL_FILE}" SHELL_HEX HEX)
    string(FIND "${SHELL_HEX}" "0d0a" CRLF_POSITION)
    if(NOT CRLF_POSITION EQUAL -1)
        file(RELATIVE_PATH SHELL_FILE_RELATIVE "${SOURCE_ROOT}" "${SHELL_FILE}")
        file(REMOVE_RECURSE "${EXTRACT_ROOT}")
        message(FATAL_ERROR "Source archive contains CRLF shell script: ${SHELL_FILE_RELATIVE}")
    endif()
endforeach()

file(GLOB_RECURSE ARCHIVE_ENTRIES LIST_DIRECTORIES true "${SOURCE_ROOT}/*")
foreach(ARCHIVE_ENTRY IN LISTS ARCHIVE_ENTRIES)
    get_filename_component(ENTRY_NAME "${ARCHIVE_ENTRY}" NAME)
    if(ENTRY_NAME STREQUAL ".git")
        file(REMOVE_RECURSE "${EXTRACT_ROOT}")
        message(FATAL_ERROR "Source archive contains .git metadata")
    endif()
endforeach()

file(READ "${SOURCE_ROOT}/source-manifest.json" SOURCE_MANIFEST)
execute_process(
    COMMAND git -C "${REPOSITORY_ROOT}" rev-parse HEAD
    OUTPUT_VARIABLE EXPECTED_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE GIT_RESULT)
if(NOT GIT_RESULT EQUAL 0 OR NOT SOURCE_MANIFEST MATCHES "${EXPECTED_COMMIT}")
    file(REMOVE_RECURSE "${EXTRACT_ROOT}")
    message(FATAL_ERROR "Source manifest root commit does not match the checkout")
endif()

execute_process(
    COMMAND git -C "${REPOSITORY_ROOT}" submodule status --recursive
    OUTPUT_VARIABLE SUBMODULE_STATUS OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE SUBMODULE_RESULT)
if(NOT SUBMODULE_RESULT EQUAL 0)
    file(REMOVE_RECURSE "${EXTRACT_ROOT}")
    message(FATAL_ERROR "Could not inspect checkout submodules")
endif()
string(REPLACE "\r\n" "\n" SUBMODULE_STATUS "${SUBMODULE_STATUS}")
string(REPLACE "\n" ";" SUBMODULE_LINES "${SUBMODULE_STATUS}")
foreach(LINE IN LISTS SUBMODULE_LINES)
    if(LINE MATCHES "^[ +U-]?([0-9a-f]+) ([^ ]+)")
        set(SHA "${CMAKE_MATCH_1}")
        set(PATH "${CMAKE_MATCH_2}")
        if(NOT SOURCE_MANIFEST MATCHES "${SHA}" OR NOT SOURCE_MANIFEST MATCHES "${PATH}")
            file(REMOVE_RECURSE "${EXTRACT_ROOT}")
            message(FATAL_ERROR "Source manifest is missing ${PATH} at ${SHA}")
        endif()
    endif()
endforeach()

file(REMOVE_RECURSE "${EXTRACT_ROOT}")
message(STATUS "Verified complete corresponding source archive ${SOURCE_ARCHIVE}")
