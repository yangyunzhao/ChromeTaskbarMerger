#include "app_config.h"
#include "process_identity.h"
#include "recovery_journal.h"
#include "single_instance.h"
#include "tray_app.h"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
}

void TestValidConfigurationLoadsScanInterval() {
    TemporaryDirectory directory;
    if (!directory.created()) {
        return;
    }
    const std::filesystem::path path = directory.path() / L"valid.ini";
    WriteTextFile(
        path,
        "[ChromeTaskbarMerger]\r\nscan_interval_ms=1250\r\n");
    const ctm::AppConfigLoadResult result = ctm::LoadAppConfig(path);
    Expect(result.file_found && result.read_succeeded,
           "a valid configuration should load successfully");
    Expect(result.config.scan_interval == std::chrono::milliseconds(1250),
           "the configured scan interval should be applied");
    Expect(result.warnings.empty(),
           "a valid configuration should not emit warnings");
}

void TestInvalidConfigurationFallsBackSafely() {
    TemporaryDirectory directory;
    if (!directory.created()) {
        return;
    }
    const std::filesystem::path path = directory.path() / L"invalid.ini";
    WriteTextFile(
        path,
        "[ChromeTaskbarMerger]\nscan_interval_ms=10\nunknown=true\n");
    const ctm::AppConfigLoadResult result = ctm::LoadAppConfig(path);
    Expect(result.config.scan_interval == ctm::kDefaultScanInterval,
           "an unsafe scan interval should retain the default");
    Expect(result.warnings.size() == 2,
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
    TestValidConfigurationLoadsScanInterval();
    TestInvalidConfigurationFallsBackSafely();
    TestRecoverySerializationRoundTrip();
    TestCorruptRecoveryDataIsRejectedAsAWhole();
    TestRecoveryJournalSavesAtomicallyAndClears();
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
