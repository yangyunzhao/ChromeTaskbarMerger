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
