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

add_executable(
    ChromeTaskbarMergerManagementStateTests
    management_state_tests.cpp
)

target_link_libraries(
    ChromeTaskbarMergerManagementStateTests
    PRIVATE
        ctm_core
)
ctm_enable_warnings(ChromeTaskbarMergerManagementStateTests)

add_test(
    NAME management_state
    COMMAND ChromeTaskbarMergerManagementStateTests
)

add_executable(
    ChromeTaskbarMergerTabGroupModelTests
    tab_group_model_tests.cpp
)

target_link_libraries(
    ChromeTaskbarMergerTabGroupModelTests
    PRIVATE
        ctm_core
)
ctm_enable_warnings(ChromeTaskbarMergerTabGroupModelTests)

add_test(
    NAME v2_tab_group_model
    COMMAND ChromeTaskbarMergerTabGroupModelTests
)

add_executable(
    ChromeTaskbarMergerTabStripLayoutTests
    tab_strip_layout_tests.cpp
)

target_link_libraries(
    ChromeTaskbarMergerTabStripLayoutTests
    PRIVATE
        ctm_core
)
ctm_enable_warnings(ChromeTaskbarMergerTabStripLayoutTests)

add_test(
    NAME v2_tab_strip_layout
    COMMAND ChromeTaskbarMergerTabStripLayoutTests
)

add_executable(
    ChromeTaskbarMergerWindowGroupIntegrationTests
    window_group_integration_tests.cpp
)

target_link_libraries(
    ChromeTaskbarMergerWindowGroupIntegrationTests
    PRIVATE
        ctm_core
)
ctm_enable_warnings(ChromeTaskbarMergerWindowGroupIntegrationTests)

add_test(
    NAME v2_window_group_integration
    COMMAND ChromeTaskbarMergerWindowGroupIntegrationTests
)

add_executable(
    ChromeTaskbarMergerChromeWindowRegistryTests
    chrome_window_registry_tests.cpp
)

target_link_libraries(
    ChromeTaskbarMergerChromeWindowRegistryTests
    PRIVATE
        ctm_core
)
ctm_enable_warnings(ChromeTaskbarMergerChromeWindowRegistryTests)

add_test(
    NAME v2_chrome_window_registry
    COMMAND ChromeTaskbarMergerChromeWindowRegistryTests
)

add_executable(
    ChromeTaskbarMergerLifecycleSyncTests
    lifecycle_sync_tests.cpp
)

target_link_libraries(
    ChromeTaskbarMergerLifecycleSyncTests
    PRIVATE
        ctm_core
)
ctm_enable_warnings(ChromeTaskbarMergerLifecycleSyncTests)

add_test(
    NAME v2_lifecycle_sync
    COMMAND ChromeTaskbarMergerLifecycleSyncTests
)

add_executable(
    ChromeTaskbarMergerManifestTests
    manifest_tests.cpp
)

target_compile_definitions(
    ChromeTaskbarMergerManifestTests
    PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)
ctm_enable_warnings(ChromeTaskbarMergerManifestTests)

add_test(
    NAME v2_phase3_manifest
    COMMAND
        ChromeTaskbarMergerManifestTests
        $<TARGET_FILE:ChromeTaskbarMerger>
)

add_executable(
    ChromeTaskbarMergerGroupRecoveryTests
    group_recovery_tests.cpp
)

target_link_libraries(
    ChromeTaskbarMergerGroupRecoveryTests
    PRIVATE
        ctm_core
)
ctm_enable_warnings(ChromeTaskbarMergerGroupRecoveryTests)

add_test(
    NAME v2_group_recovery
    COMMAND ChromeTaskbarMergerGroupRecoveryTests
)

add_executable(
    ChromeTaskbarMergerTabNameStoreTests
    tab_name_store_tests.cpp
)

target_link_libraries(
    ChromeTaskbarMergerTabNameStoreTests
    PRIVATE
        ctm_core
)
ctm_enable_warnings(ChromeTaskbarMergerTabNameStoreTests)

add_test(
    NAME v2_tab_name_store
    COMMAND ChromeTaskbarMergerTabNameStoreTests
)

add_executable(
    ChromeTaskbarMergerChromeProfileResolverTests
    chrome_profile_resolver_tests.cpp
)

target_link_libraries(
    ChromeTaskbarMergerChromeProfileResolverTests
    PRIVATE
        ctm_core
)
ctm_enable_warnings(ChromeTaskbarMergerChromeProfileResolverTests)

add_test(
    NAME v2_chrome_profile_resolver
    COMMAND ChromeTaskbarMergerChromeProfileResolverTests
)
