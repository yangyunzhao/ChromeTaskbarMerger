#include "app_config.h"
#include "process_identity.h"
#include "recovery_journal.h"
#include "restore_command.h"
#include "single_instance.h"
#include "tray_app.h"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;
ctm::ExistingInstanceCommand received_tray_command =
    ctm::ExistingInstanceCommand::Rescan;
int received_tray_command_count = 0;

LRESULT CALLBACK TestTrayWindowProcedure(const HWND window,
                                         const UINT message,
                                         const WPARAM wparam,
                                         const LPARAM lparam) {
    if (message == ctm::kExternalCommandMessage) {
        received_tray_command =
            static_cast<ctm::ExistingInstanceCommand>(wparam);
        ++received_tray_command_count;
        return 1;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void Expect(const bool condition, const std::string_view description) {
    if (condition) {
        return;
    }
    std::cerr << "FAILED: " << description << '\n';
    ++failures;
}

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                (L"ChromeTaskbarMergerTests-" +
                 std::to_wstring(GetCurrentProcessId()) + L"-" +
                 std::to_wstring(GetTickCount64()));
        std::error_code error;
        std::filesystem::create_directories(path_, error);
        created_ = !error;
    }

    ~TemporaryDirectory() {
        if (!created_) {
            return;
        }
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    [[nodiscard]] bool created() const noexcept {
        return created_;
    }

private:
    std::filesystem::path path_;
    bool created_ = false;
};

void WriteTextFile(const std::filesystem::path& path,
                   const std::string_view content) {
    std::ofstream output(path, std::ios::out | std::ios::binary);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
}

[[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

[[nodiscard]] std::size_t CountOccurrences(
    const std::string_view text,
    const std::string_view value) {
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(value, position)) != std::string_view::npos) {
        ++count;
        position += value.size();
    }
    return count;
}

[[nodiscard]] ctm::TaskbarMutationState MakeRecoveryState(
    const std::uintptr_t hwnd,
    const DWORD process_id,
    const std::uint64_t creation_time) {
    ctm::TaskbarMutationState state;
    state.method = ctm::TaskbarMethod::TaskbarList;
    state.identity.hwnd = reinterpret_cast<HWND>(hwnd);
    state.identity.process_id = process_id;
    state.identity.thread_id = process_id + 10;
    state.identity.process_creation_time = creation_time;
    state.identity.class_name = L"Chrome_WidgetWin_1";
    state.original_extended_style = WS_EX_APPWINDOW;
    state.applied_extended_style = WS_EX_TOOLWINDOW;
    state.modification_applied = true;
    return state;
}

void TestMissingConfigurationUsesDefaults() {
    TemporaryDirectory directory;
    Expect(directory.created(), "the temporary configuration directory should exist");
    if (!directory.created()) {
        return;
    }
    const ctm::AppConfigLoadResult result = ctm::LoadAppConfig(
        directory.path() / L"missing.ini");
    Expect(!result.file_found,
           "a missing configuration should be reported as absent");
    Expect(result.read_succeeded,
           "a missing optional configuration should not be an error");
    Expect(result.config.scan_interval == ctm::kDefaultScanInterval,
           "a missing configuration should use the two-second default");
    Expect(result.config.windowtabs_check_interval ==
               ctm::kDefaultWindowTabsCheckInterval,
           "a missing configuration should use the three-second WindowTabs "
           "check default");
    Expect(!result.config.start_with_windows,
           "a missing configuration should keep login startup disabled");
    Expect(result.config.tab_provider == ctm::TabProvider::BuiltIn,
           "a missing V1 configuration should migrate to built-in tabs");
    Expect(!result.config.persist_tab_names_by_profile,
           "profile-linked tab names should be opt-in by default");
    Expect(result.config.tab_strip_alignment ==
               ctm::TabStripAlignment::Right &&
               result.config.tab_strip_width_percent == 60 &&
               result.config.tab_width_pixels == 180,
           "a missing configuration should use safe built-in layout defaults");
}

void TestOnlyBuiltInProviderSupportsNameEditing() {
    Expect(ctm::TabProviderSupportsInMemoryNameEditing(
               ctm::TabProvider::BuiltIn),
           "the built-in provider should expose in-memory name editing");
    Expect(!ctm::TabProviderSupportsInMemoryNameEditing(
               ctm::TabProvider::WindowTabs),
           "the WindowTabs provider must not expose unsupported name editing");
}

void TestValidConfigurationLoadsScanInterval() {
    TemporaryDirectory directory;
    if (!directory.created()) {
        return;
    }
    const std::filesystem::path path = directory.path() / L"valid.ini";
    WriteTextFile(
        path,
        "[ChromeTaskbarMerger]\r\nscan_interval_ms=1250\r\n"
        "windowtabs_check_interval_ms=1750\r\n"
        "start_with_windows=TrUe\r\n"
        "tab_provider=WindowTabs\r\n"
        "persist_tab_names_by_profile=TrUe\r\n"
        "tab_strip_alignment=right\r\n"
        "tab_strip_width_percent=75\r\n"
        "tab_width_px=220\r\n");
    const ctm::AppConfigLoadResult result = ctm::LoadAppConfig(path);
    Expect(result.file_found && result.read_succeeded,
           "a valid configuration should load successfully");
    Expect(result.config.scan_interval == std::chrono::milliseconds(1250),
           "the configured scan interval should be applied");
    Expect(result.config.windowtabs_check_interval ==
               std::chrono::milliseconds(1750),
           "the configured WindowTabs check interval should be applied");
    Expect(result.config.start_with_windows,
           "a case-insensitive true value should enable login startup");
    Expect(result.config.tab_provider == ctm::TabProvider::WindowTabs,
           "a case-insensitive WindowTabs provider should load");
    Expect(result.config.persist_tab_names_by_profile,
           "a case-insensitive true value should enable profile name persistence");
    Expect(result.config.tab_strip_alignment ==
               ctm::TabStripAlignment::Right &&
               result.config.tab_strip_width_percent == 75 &&
               result.config.tab_width_pixels == 220,
           "valid built-in tab layout settings should load");
    Expect(result.warnings.empty(),
           "a valid configuration should not emit warnings");
}

void TestTabProviderSettingSavesAtomicallyAndPreservesV1Keys() {
    TemporaryDirectory directory;
    if (!directory.created()) {
        return;
    }
    const std::filesystem::path path = directory.path() / L"provider.ini";
    WriteTextFile(
        path,
        "; preserve provider comment\r\n"
        "[ChromeTaskbarMerger]\r\n"
        "scan_interval_ms=1250\r\n"
        "windowtabs_check_interval_ms=1750\r\n"
        "tab_provider=builtin\r\n"
        "tab_provider=builtin\r\n"
        "start_with_windows=true\r\n"
        "[OtherSection]\r\nkeep=value\r\n");

    const ctm::AppConfigSaveResult save =
        ctm::SaveTabProviderSetting(path, ctm::TabProvider::WindowTabs);
    const ctm::AppConfigLoadResult loaded = ctm::LoadAppConfig(path);
    Expect(save.succeeded && loaded.read_succeeded &&
               loaded.config.tab_provider == ctm::TabProvider::WindowTabs,
           "the selected tab provider should survive an atomic reload");
    Expect(loaded.config.start_with_windows &&
               loaded.config.scan_interval ==
                   std::chrono::milliseconds(1250) &&
               loaded.config.windowtabs_check_interval ==
                   std::chrono::milliseconds(1750),
           "saving the provider should preserve V1-compatible settings");
    const std::string text = ReadTextFile(path);
    Expect(CountOccurrences(text, "tab_provider=") == 1 &&
               text.find("; preserve provider comment") !=
                   std::string::npos &&
               text.find("keep=value") != std::string::npos,
           "saving the provider should remove duplicates and preserve unrelated text");
}

void TestInvalidV2SettingsFallBackWithoutDiscardingValidValues() {
    TemporaryDirectory directory;
    if (!directory.created()) {
        return;
    }
    const std::filesystem::path path = directory.path() / L"invalid-v2.ini";
    WriteTextFile(
        path,
        "[ChromeTaskbarMerger]\n"
        "tab_provider=unknown\n"
        "persist_tab_names_by_profile=maybe\n"
        "tab_strip_alignment=middle\n"
        "tab_strip_width_percent=24\n"
        "tab_width_px=401\n"
        "scan_interval_ms=1500\n");
    const ctm::AppConfigLoadResult result = ctm::LoadAppConfig(path);
    Expect(result.config.tab_provider == ctm::TabProvider::BuiltIn &&
               result.config.tab_strip_alignment ==
                   ctm::TabStripAlignment::Right &&
               result.config.tab_strip_width_percent == 60 &&
               result.config.tab_width_pixels == 180,
           "invalid V2 settings should retain their safe defaults");
    Expect(result.config.scan_interval == std::chrono::milliseconds(1500),
           "an invalid V2 setting must not discard an unrelated valid setting");
    Expect(!result.config.persist_tab_names_by_profile,
           "an invalid profile persistence value should retain the opt-in default");
    Expect(result.warnings.size() == 5,
           "each invalid V2 setting should emit one precise warning");
}

void TestProfileNamePersistenceSettingSavesAtomically() {
    TemporaryDirectory directory;
    if (!directory.created()) {
        return;
    }
    const std::filesystem::path path = directory.path() / L"profile-names.ini";
    WriteTextFile(
        path,
        "; keep profile setting comment\r\n"
        "[ChromeTaskbarMerger]\r\n"
        "tab_provider=builtin\r\n"
        "persist_tab_names_by_profile=false\r\n"
        "persist_tab_names_by_profile=false\r\n"
        "scan_interval_ms=1250\r\n");
    const ctm::AppConfigSaveResult save =
        ctm::SaveProfileTabNamePersistenceSetting(path, true);
    const ctm::AppConfigLoadResult loaded = ctm::LoadAppConfig(path);
    const std::string text = ReadTextFile(path);
    Expect(save.succeeded && loaded.read_succeeded &&
               loaded.config.persist_tab_names_by_profile,
           "profile name persistence should survive an atomic reload");
    Expect(loaded.config.tab_provider == ctm::TabProvider::BuiltIn &&
               loaded.config.scan_interval == std::chrono::milliseconds(1250),
           "saving profile persistence should preserve unrelated settings");
    Expect(CountOccurrences(text, "persist_tab_names_by_profile=") == 1 &&
               text.find("; keep profile setting comment") != std::string::npos,
           "saving profile persistence should remove duplicates and preserve comments");
}

void TestTabProviderSaveFailureLeavesNoAmbiguousSuccess() {
    const ctm::AppConfigSaveResult failed =
        ctm::SaveTabProviderSetting({}, ctm::TabProvider::WindowTabs);
    Expect(!failed.succeeded && !failed.error_message.empty(),
           "an unavailable provider configuration path should fail closed");
    Expect(ctm::TabProviderConfigValue(ctm::TabProvider::BuiltIn) ==
               "builtin" &&
               ctm::TabProviderConfigValue(ctm::TabProvider::WindowTabs) ==
                   "windowtabs",
           "provider persistence should use stable documented values");
}

void TestStartWithWindowsSettingSavesWithoutDiscardingConfiguration() {
    TemporaryDirectory directory;
    if (!directory.created()) {
        return;
    }
    const std::filesystem::path path = directory.path() / L"便携配置.ini";
    WriteTextFile(
        path,
        "; keep this comment\r\n"
        "[ChromeTaskbarMerger]\r\n"
        "scan_interval_ms=1250\r\n"
        "windowtabs_check_interval_ms=1750\r\n"
        "start_with_windows=false\r\n"
        "start_with_windows=false\r\n"
        "[OtherSection]\r\n"
        "preserve=this-value\r\n");

    ctm::AppConfigSaveResult save =
        ctm::SaveStartWithWindowsSetting(path, true);
    Expect(save.succeeded,
           "the tray setting should save through an atomic replacement");
    ctm::AppConfigLoadResult loaded = ctm::LoadAppConfig(path);
    Expect(loaded.read_succeeded && loaded.config.start_with_windows,
           "the saved true value should load after an application restart");
    Expect(loaded.config.scan_interval == std::chrono::milliseconds(1250),
           "saving startup should preserve the configured scan interval");
    Expect(loaded.config.windowtabs_check_interval ==
               std::chrono::milliseconds(1750),
           "saving startup should preserve the WindowTabs check interval");

    const std::string enabled_text = ReadTextFile(path);
    Expect(enabled_text.find("; keep this comment") != std::string::npos &&
               enabled_text.find("preserve=this-value") != std::string::npos,
           "saving startup should preserve comments and unrelated sections");
    Expect(CountOccurrences(enabled_text, "start_with_windows=") == 1,
           "saving startup should remove ambiguous duplicate settings");

    save = ctm::SaveStartWithWindowsSetting(path, false);
    loaded = ctm::LoadAppConfig(path);
    Expect(save.succeeded && !loaded.config.start_with_windows,
           "saving false should disable startup in the persisted setting");

    bool temporary_file_left_behind = false;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory.path())) {
        if (entry.path().filename().wstring().find(L".tmp-") !=
            std::wstring::npos) {
            temporary_file_left_behind = true;
        }
    }
    Expect(!temporary_file_left_behind,
           "a successful atomic save should not leave a temporary file");
}

void TestSavingCreatesAMissingPortableConfiguration() {
    TemporaryDirectory directory;
    if (!directory.created()) {
        return;
    }
    const std::filesystem::path path = directory.path() / L"new.ini";
    const ctm::AppConfigSaveResult save =
        ctm::SaveStartWithWindowsSetting(path, true);
    const ctm::AppConfigLoadResult loaded = ctm::LoadAppConfig(path);
    Expect(save.succeeded && loaded.file_found &&
               loaded.config.start_with_windows,
           "saving should create a missing portable configuration");
    Expect(loaded.config.scan_interval == ctm::kDefaultScanInterval,
           "a newly created configuration should retain the scan default");
    Expect(loaded.config.windowtabs_check_interval ==
               ctm::kDefaultWindowTabsCheckInterval,
           "a newly created configuration should retain the WindowTabs check "
           "default");
}

void TestInvalidStartWithWindowsValueFailsClosed() {
    TemporaryDirectory directory;
    if (!directory.created()) {
        return;
    }
    const std::filesystem::path path = directory.path() / L"invalid-start.ini";
    WriteTextFile(
        path,
        "[ChromeTaskbarMerger]\nstart_with_windows=perhaps\n");
    const ctm::AppConfigLoadResult result = ctm::LoadAppConfig(path);
    Expect(!result.config.start_with_windows,
           "an invalid startup value should retain the safe disabled default");
    Expect(result.warnings.size() == 1,
           "an invalid startup value should emit a precise warning");
}

void TestInvalidConfigurationFallsBackSafely() {
    TemporaryDirectory directory;
    if (!directory.created()) {
        return;
    }
    const std::filesystem::path path = directory.path() / L"invalid.ini";
    WriteTextFile(
        path,
        "[ChromeTaskbarMerger]\nscan_interval_ms=10\n"
        "windowtabs_check_interval_ms=70000\nunknown=true\n");
    const ctm::AppConfigLoadResult result = ctm::LoadAppConfig(path);
    Expect(result.config.scan_interval == ctm::kDefaultScanInterval,
           "an unsafe scan interval should retain the default");
    Expect(result.config.windowtabs_check_interval ==
               ctm::kDefaultWindowTabsCheckInterval,
           "an unsafe WindowTabs check interval should retain the default");
    Expect(result.warnings.size() == 3,
           "invalid and unknown configuration values should be explained");
}

void TestRecoverySerializationRoundTrip() {
    const std::vector states = {
        MakeRecoveryState(0x1234, 100, 1000),
        MakeRecoveryState(0x5678, 200, 2000),
    };
    const std::string serialized = ctm::SerializeRecoveryStates(states);
    Expect(!serialized.empty(),
           "valid recovery states should serialize");
    const ctm::RecoveryParseResult parsed =
        ctm::ParseRecoveryStates(serialized);
    Expect(parsed.succeeded && parsed.states.size() == 2,
           "serialized recovery states should parse completely");
    if (parsed.states.size() == 2) {
        Expect(parsed.states[0].identity.process_creation_time == 1000,
               "process creation time should survive serialization");
        Expect(parsed.states[1].identity.hwnd ==
                   reinterpret_cast<HWND>(0x5678),
               "the HWND value should survive serialization");
        Expect(parsed.states[1].NeedsRestore(),
               "parsed records should retain a restoration obligation");
    }
}

void TestCorruptRecoveryDataIsRejectedAsAWhole() {
    const ctm::RecoveryParseResult bad_header =
        ctm::ParseRecoveryStates("not-a-journal\n");
    Expect(!bad_header.succeeded && bad_header.states.empty(),
           "an unknown recovery header should be rejected without states");

    const ctm::RecoveryParseResult truncated = ctm::ParseRecoveryStates(
        "ChromeTaskbarMergerRecovery\t1\nstate\ttaskbar_list\t1234\n");
    Expect(!truncated.succeeded && truncated.states.empty(),
           "a truncated recovery record should not be partially adopted");

    const ctm::TaskbarMutationState state =
        MakeRecoveryState(0x1234, 100, 1000);
    const std::string record = ctm::SerializeRecoveryStates(
        std::vector{state});
    const std::size_t first_newline = record.find('\n');
    const std::string one_line = record.substr(first_newline + 1);
    const ctm::RecoveryParseResult duplicate = ctm::ParseRecoveryStates(
        record + one_line);
    Expect(!duplicate.succeeded && duplicate.states.empty(),
           "duplicate identities should reject the whole journal");
}

void TestRecoveryJournalSavesAtomicallyAndClears() {
    TemporaryDirectory directory;
    if (!directory.created()) {
        return;
    }
    const std::filesystem::path path = directory.path() / L"recovery.tsv";
    ctm::RecoveryJournal journal(path);
    const std::vector states = {MakeRecoveryState(0x1234, 100, 1000)};
    std::wstring error;
    Expect(journal.Save(states, &error),
           "a recovery journal should save a valid state");
    ctm::RecoveryLoadResult loaded = journal.Load();
    Expect(loaded.succeeded && loaded.file_found && loaded.states.size() == 1,
           "a saved recovery journal should load its state");

    const std::vector<ctm::TaskbarMutationState> empty;
    Expect(journal.Save(empty, &error),
           "saving an empty recovery state should succeed");
    loaded = journal.Load();
    Expect(loaded.succeeded && loaded.states.empty(),
           "an empty journal should clear all recovery obligations");
}

void TestStartupTaskbarRecoveryIsSafeAndIdempotent() {
    TemporaryDirectory directory;
    if (!directory.created()) {
        return;
    }
    const std::filesystem::path path =
        directory.path() / L"startup-recovery.tsv";
    ctm::RecoveryJournal journal(path);
    const std::vector states = {
        MakeRecoveryState(0x1234, 100, 1000),
    };
    std::wstring error;
    Expect(journal.Save(states, &error),
           "a stale V1 recovery obligation should persist for startup testing");

    const ctm::StartupTaskbarRecoveryResult restored =
        ctm::RestorePreviousTaskbarSession(nullptr, path);
    const ctm::RecoveryLoadResult cleared = journal.Load();
    Expect(restored.succeeded && restored.recovery_attempted &&
               cleared.succeeded && cleared.states.empty(),
           "startup should safely skip a stale HWND and clear its V1 recovery obligation");

    const ctm::StartupTaskbarRecoveryResult repeated =
        ctm::RestorePreviousTaskbarSession(nullptr, path);
    Expect(repeated.succeeded && !repeated.recovery_attempted,
           "repeating startup V1 recovery after cleanup should be a no-op");

    WriteTextFile(path, "not-a-v1-recovery-journal\n");
    const ctm::StartupTaskbarRecoveryResult invalid =
        ctm::RestorePreviousTaskbarSession(nullptr, path);
    Expect(!invalid.succeeded && !invalid.error_message.empty(),
           "an invalid V1 startup journal should fail closed with an explanation");
}

void TestSingleInstanceMutexDistinguishesPrimaryAndExisting() {
    const std::wstring name =
        L"Local\\ChromeTaskbarMerger.Test." +
        std::to_wstring(GetCurrentProcessId()) + L'.' +
        std::to_wstring(GetTickCount64());
    {
        ctm::SingleInstanceGuard first;
        ctm::SingleInstanceGuard second;
        DWORD error = ERROR_SUCCESS;
        Expect(first.Acquire(name, &error) ==
                   ctm::SingleInstanceStatus::Primary,
               "the first named-mutex owner should be primary");
        Expect(second.Acquire(name, &error) ==
                   ctm::SingleInstanceStatus::Existing,
               "the second named-mutex owner should detect an instance");
    }

    ctm::SingleInstanceGuard after_release;
    DWORD error = ERROR_SUCCESS;
    Expect(after_release.Acquire(name, &error) ==
               ctm::SingleInstanceStatus::Primary,
           "releasing all handles should allow a new primary instance");
}

void TestCurrentProcessCreationTimeIsAvailable() {
    const ctm::ProcessCreationTimeResult result =
        ctm::QueryProcessCreationTime(GetCurrentProcessId());
    Expect(result.succeeded && result.value != 0,
           "the current process should expose a stable creation time");
}

void TestTrayInstanceCommandsCanBeSentSynchronouslyAndAsynchronously() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const std::wstring class_name =
        L"ChromeTaskbarMerger.TestTrayWindow." +
        std::to_wstring(GetCurrentProcessId());
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = TestTrayWindowProcedure;
    window_class.lpszClassName = class_name.c_str();
    const ATOM registered = RegisterClassExW(&window_class);
    Expect(registered != 0, "the test tray window class should register");
    if (registered == 0) {
        return;
    }

    const HWND window = CreateWindowExW(
        0,
        class_name.c_str(),
        L"ChromeTaskbarMerger test tray",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        instance,
        nullptr);
    Expect(window != nullptr, "the test tray message window should exist");
    if (window != nullptr) {
        received_tray_command_count = 0;
        DWORD error = ERROR_SUCCESS;
        Expect(ctm::SendTrayInstanceCommand(
                   window,
                   ctm::ExistingInstanceCommand::RestoreAll,
                   true,
                   &error),
               "a synchronous restore command should be acknowledged");
        Expect(error == ERROR_SUCCESS && received_tray_command_count == 1 &&
                   received_tray_command ==
                       ctm::ExistingInstanceCommand::RestoreAll,
               "the synchronous command should reach the existing instance");

        Expect(ctm::SendTrayInstanceCommand(
                   window,
                   ctm::ExistingInstanceCommand::Rescan,
                   false,
                   &error),
               "an asynchronous rescan command should be posted");
        MSG message{};
        while (PeekMessageW(&message, window, 0, 0, PM_REMOVE) != FALSE) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Expect(error == ERROR_SUCCESS && received_tray_command_count == 2 &&
                   received_tray_command ==
                       ctm::ExistingInstanceCommand::Rescan,
               "the asynchronous command should reach the existing instance");
        DestroyWindow(window);
    }
    UnregisterClassW(class_name.c_str(), instance);
}

}  // namespace

int main() {
    TestMissingConfigurationUsesDefaults();
    TestOnlyBuiltInProviderSupportsNameEditing();
    TestValidConfigurationLoadsScanInterval();
    TestInvalidConfigurationFallsBackSafely();
    TestStartWithWindowsSettingSavesWithoutDiscardingConfiguration();
    TestTabProviderSettingSavesAtomicallyAndPreservesV1Keys();
    TestInvalidV2SettingsFallBackWithoutDiscardingValidValues();
    TestProfileNamePersistenceSettingSavesAtomically();
    TestTabProviderSaveFailureLeavesNoAmbiguousSuccess();
    TestSavingCreatesAMissingPortableConfiguration();
    TestInvalidStartWithWindowsValueFailsClosed();
    TestRecoverySerializationRoundTrip();
    TestCorruptRecoveryDataIsRejectedAsAWhole();
    TestRecoveryJournalSavesAtomicallyAndClears();
    TestStartupTaskbarRecoveryIsSafeAndIdempotent();
    TestSingleInstanceMutexDistinguishesPrimaryAndExisting();
    TestCurrentProcessCreationTimeIsAvailable();
    TestTrayInstanceCommandsCanBeSentSynchronouslyAndAsynchronously();

    if (failures != 0) {
        std::cerr << failures << " runtime-support test(s) failed.\n";
        return 1;
    }
    std::cout << "All runtime-support tests passed.\n";
    return 0;
}
