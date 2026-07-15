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
    NAME command_line
    COMMAND ChromeTaskbarMergerTests
)
