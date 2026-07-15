add_executable(
    ChromeTaskbarMergerTests
    command_line_tests.cpp
)

target_link_libraries(
    ChromeTaskbarMergerTests
    PRIVATE
        ctm_core
)
ctm_enable_warnings(ChromeTaskbarMergerTests)

add_test(
    NAME core
    COMMAND ChromeTaskbarMergerTests
)

add_executable(
    ChromeTaskbarMergerFixedEntryTests
    fixed_entry_manager_tests.cpp
)

target_link_libraries(
    ChromeTaskbarMergerFixedEntryTests
    PRIVATE
        ctm_core
)
ctm_enable_warnings(ChromeTaskbarMergerFixedEntryTests)

add_test(
    NAME fixed_entry
    COMMAND ChromeTaskbarMergerFixedEntryTests
)

add_executable(
    ChromeTaskbarMergerScanScheduleTests
    scan_schedule_tests.cpp
)

target_link_libraries(
    ChromeTaskbarMergerScanScheduleTests
    PRIVATE
        ctm_core
)
ctm_enable_warnings(ChromeTaskbarMergerScanScheduleTests)

add_test(
    NAME scan_schedule
    COMMAND ChromeTaskbarMergerScanScheduleTests
)

add_executable(
    ChromeTaskbarMergerRuntimeSupportTests
    runtime_support_tests.cpp
)

target_link_libraries(
    ChromeTaskbarMergerRuntimeSupportTests
    PRIVATE
        ctm_core
)
ctm_enable_warnings(ChromeTaskbarMergerRuntimeSupportTests)

add_test(
    NAME runtime_support
    COMMAND ChromeTaskbarMergerRuntimeSupportTests
)

add_executable(
    ChromeTaskbarMergerAutoStartTests
    auto_start_tests.cpp
)

target_link_libraries(
    ChromeTaskbarMergerAutoStartTests
    PRIVATE
        ctm_core
)
ctm_enable_warnings(ChromeTaskbarMergerAutoStartTests)

add_test(
    NAME auto_start
    COMMAND ChromeTaskbarMergerAutoStartTests
)
