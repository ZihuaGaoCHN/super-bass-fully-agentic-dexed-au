cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED LOG_ROOT OR NOT IS_DIRECTORY "${LOG_ROOT}")
    message(FATAL_ERROR "LOG_ROOT must name an existing validation-log directory")
endif()

file(GLOB_RECURSE log_files LIST_DIRECTORIES false "${LOG_ROOT}/*")
set(files_changed 0)
foreach(log_file IN LISTS log_files)
    file(SIZE "${log_file}" log_size)
    if(log_size GREATER 67108864)
        continue()
    endif()

    file(READ "${log_file}" original)
    set(sanitized "${original}")
    string(REGEX REPLACE
        "/(home|Users)/runner/work/[^/ \t\r\n]+/[^/ \t\r\n]+"
        "<ci-workspace>"
        sanitized "${sanitized}")
    string(REGEX REPLACE
        "[A-Za-z]:[/\\\\]a[/\\\\][^/\\\\ \t\r\n]+[/\\\\][^/\\\\ \t\r\n]+"
        "<ci-workspace>"
        sanitized "${sanitized}")

    if(NOT sanitized STREQUAL original)
        file(WRITE "${log_file}" "${sanitized}")
        math(EXPR files_changed "${files_changed} + 1")
    endif()
endforeach()

message(STATUS "Sanitized ${files_changed} validation log file(s)")
