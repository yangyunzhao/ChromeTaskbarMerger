cmake_minimum_required(VERSION 3.24)

project(
    ChromeTaskbarMerger
    VERSION 0.1.0
    DESCRIPTION "Keep a single Chrome entry on the Windows taskbar"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "ChromeTaskbarMerger supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ctm/version.h"
    @ONLY
)

function(ctm_enable_warnings target_name)
    if(MSVC)
        target_compile_options(
            ${target_name}
            PRIVATE
                /W4
                /permissive-
                /utf-8
        )
    else()
        target_compile_options(
            ${target_name}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
        )
    endif()
endfunction()

add_library(
    ctm_core
    STATIC
        src/chrome_window.cpp
        src/chrome_window.h
        src/command_line.cpp
        src/command_line.h
        src/fixed_entry_app.cpp
        src/fixed_entry_app.h
        src/fixed_entry_manager.cpp
        src/fixed_entry_manager.h
        src/logger.cpp
        src/logger.h
        src/scan_schedule.cpp
        src/scan_schedule.h
        src/taskbar_controller.cpp
        src/taskbar_controller.h
        src/taskbar_experiment.cpp
        src/taskbar_experiment.h
)

target_compile_features(ctm_core PUBLIC cxx_std_20)
target_compile_definitions(
    ctm_core
    PUBLIC
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)
target_include_directories(
    ctm_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
ctm_enable_warnings(ctm_core)
target_link_libraries(ctm_core PUBLIC Dwmapi Ole32)

add_executable(
    ChromeTaskbarMerger
    src/main.cpp
)

target_link_libraries(
    ChromeTaskbarMerger
    PRIVATE
        ctm_core
        Shell32
)
set_target_properties(
    ChromeTaskbarMerger
    PROPERTIES
        WIN32_EXECUTABLE "$<CONFIG:Release>"
)
ctm_enable_warnings(ChromeTaskbarMerger)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
