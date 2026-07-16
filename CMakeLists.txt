cmake_minimum_required(VERSION 3.24)

project(
    ChromeTaskbarMerger
    VERSION 1.0.0
    DESCRIPTION "Keep a single Chrome entry on the Windows taskbar"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "ChromeTaskbarMerger supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(
        CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ctm/version.h"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc"
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
        src/app_config.cpp
        src/app_config.h
        src/app_paths.cpp
        src/app_paths.h
        src/auto_start.cpp
        src/auto_start.h
        src/command_line.cpp
        src/command_line.h
        src/fixed_entry_app.cpp
        src/fixed_entry_app.h
        src/fixed_entry_manager.cpp
        src/fixed_entry_manager.h
        src/logger.cpp
        src/logger.h
        src/management_state.cpp
        src/management_state.h
        src/process_identity.cpp
        src/process_identity.h
        src/recovery_journal.cpp
        src/recovery_journal.h
        src/restore_command.cpp
        src/restore_command.h
        src/scan_schedule.cpp
        src/scan_schedule.h
        src/single_instance.cpp
        src/single_instance.h
        src/taskbar_controller.cpp
        src/taskbar_controller.h
        src/taskbar_experiment.cpp
        src/taskbar_experiment.h
        src/tab_activation.cpp
        src/tab_activation.h
        src/tab_group_model.cpp
        src/tab_group_model.h
        src/tray_app.cpp
        src/tray_app.h
        src/window_identity.cpp
        src/window_identity.h
        src/windowtabs_presence.cpp
        src/windowtabs_presence.h
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
target_link_libraries(ctm_core PUBLIC Advapi32 Dwmapi Ole32)

add_executable(
    ChromeTaskbarMerger
    src/main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc"
    assets/ChromeTaskbarMerger.ico
    assets/ChromeTaskbarMerger.svg
    src/app.manifest
)

set_source_files_properties(
    assets/ChromeTaskbarMerger.ico
    assets/ChromeTaskbarMerger.svg
    src/app.manifest
    PROPERTIES HEADER_FILE_ONLY TRUE
)

target_link_libraries(
    ChromeTaskbarMerger
    PRIVATE
        ctm_core
        Shell32
)
if(MSVC)
    target_link_options(
        ChromeTaskbarMerger
        PRIVATE
            "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest"
    )
endif()
set_target_properties(
    ChromeTaskbarMerger
    PROPERTIES
        WIN32_EXECUTABLE "$<CONFIG:Release>"
)
ctm_enable_warnings(ChromeTaskbarMerger)

install(
    TARGETS ChromeTaskbarMerger
    RUNTIME DESTINATION .
)
install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/config/ChromeTaskbarMerger.ini"
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION .
)
install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/PORTABLE_README.md"
    DESTINATION .
    RENAME README.md
)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
