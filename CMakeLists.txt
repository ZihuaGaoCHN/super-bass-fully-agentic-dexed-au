cmake_minimum_required(VERSION 3.24)

if(CMAKE_HOST_APPLE)
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        if(DEFINED ENV{MACOSX_DEPLOYMENT_TARGET}
           AND NOT "$ENV{MACOSX_DEPLOYMENT_TARGET}" STREQUAL "")
            set(CMAKE_OSX_DEPLOYMENT_TARGET "$ENV{MACOSX_DEPLOYMENT_TARGET}" CACHE STRING
                "Minimum macOS version supported by Agentic Dexed")
        else()
            set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
                "Minimum macOS version supported by Agentic Dexed")
        endif()
    endif()
    set(ENV{MACOSX_DEPLOYMENT_TARGET} "${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

if(POLICY CMP0175)
    cmake_policy(SET CMP0175 OLD)
    set(CMAKE_POLICY_DEFAULT_CMP0175 OLD)
endif()
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 OLD)
    set(CMAKE_POLICY_DEFAULT_CMP0177 OLD)
endif()

include(cmake/AgenticDexedProduct.cmake)

project(${AGENTIC_DEXED_TARGET_NAME} VERSION 1.0.1)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /wd4828)
endif()

#Compile commands, useful for some IDEs like VS-Code
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

if(NOT BUILD_ID)
    add_definitions("-DDEXED_ID=DEVBUILD")
else()
    add_definitions("-DDEXED_ID=${BUILD_ID}")
endif()

#static linking in Windows
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# include JUCE *AFTER* the MSVC runtime and so on is set up
set(DEXED_JUCE_PATH "${CMAKE_SOURCE_DIR}/libs/JUCE" CACHE STRING "Path to JUCE library source tree")
add_subdirectory(${DEXED_JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

#Adds all the module sources so they appear correctly in the IDE
set_property(GLOBAL PROPERTY USE_FOLDERS YES)
option(JUCE_ENABLE_MODULE_SOURCE_GROUPS "Enable Module Source Groups" ON)

#set any of these to "ON" if you want to build one of the juce examples
#or extras (Projucer/AudioPluginHost, etc):
option(JUCE_BUILD_EXTRAS "Build JUCE Extras" OFF)
option(JUCE_BUILD_EXAMPLES "Build JUCE Examples" OFF)

add_subdirectory(libs)
add_subdirectory(assets)
add_subdirectory(Source)

option(AGENTIC_DEXED_BUILD_TESTS "Build Agentic Dexed test targets" ON)
if(AGENTIC_DEXED_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

# uncomment to enable warnings are errors,
# suppress warnings (or remove when fixed)
# or add -Wall, -Wextra or specific warnings as you see fit
if (APPLE)
target_compile_options(${PROJECT_NAME} PUBLIC
        #-Werror
        #-Wno-deprecated-declarations
        #-Wno-unused-value
        )
elseif(UNIX AND NOT APPLE)
target_compile_options(${PROJECT_NAME} PUBLIC
        #-Werror
        -Wno-deprecated-declarations
        -Wno-unused-value
        )
else()
target_compile_options(${PROJECT_NAME} PUBLIC
        #/WX # Warnings are errors
        #/wd4996 # deprecated-declarations
        # (I don't know all the windows warning numbers
        #  replace cxxx with /wdxxx to supress them)
        )
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/assets/installers")
include(installer)
