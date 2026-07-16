#include "v2_experiment.h"

#include "chrome_window.h"
#include "fixed_entry_manager.h"
#include "recovery_journal.h"
#include "tab_activation.h"
#include "tab_group_model.h"
#include "tab_strip_window.h"
#include "taskbar_controller.h"
#include "v2_taskbar_readiness.h"
#include "window_coordinator.h"
#include "window_identity_query.h"
#include "windowtabs_presence.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctm {
namespace {

class ConsoleControlGuard final {
public:
    [[nodiscard]] bool Activate() noexcept {
        if (active_) {
            return true;
        }
        active_ = SetConsoleCtrlHandler(nullptr, TRUE) != FALSE;
        return active_;
    }

    ~ConsoleControlGuard() {
        if (active_) {
            SetConsoleCtrlHandler(nullptr, FALSE);
        }
    }

    ConsoleControlGuard() = default;
    ConsoleControlGuard(const ConsoleControlGuard&) = delete;
    ConsoleControlGuard& operator=(const ConsoleControlGuard&) = delete;

private:
    bool active_ = false;
};

class ConsoleInputGuard final {
public:
    [[nodiscard]] bool Activate(DWORD* const error_code) noexcept {
        input_ = GetStdHandle(STD_INPUT_HANDLE);
        if (input_ == nullptr || input_ == INVALID_HANDLE_VALUE) {
            if (error_code != nullptr) {
                *error_code = GetLastError();
            }
            return false;
        }
        if (GetConsoleMode(input_, &original_mode_) == FALSE) {
            if (error_code != nullptr) {
                *error_code = GetLastError();
            }
            return false;
        }
        DWORD mode = original_mode_;
        mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
                  ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT |
                  ENABLE_QUICK_EDIT_MODE);
        mode |= ENABLE_EXTENDED_FLAGS;
        if (SetConsoleMode(input_, mode) == FALSE) {
            if (error_code != nullptr) {
                *error_code = GetLastError();
            }
            return false;
        }
        active_ = true;
        if (error_code != nullptr) {
            *error_code = ERROR_SUCCESS;
        }
        return true;
    }

    ~ConsoleInputGuard() {
        if (active_) {
            SetConsoleMode(input_, original_mode_);
        }
    }

    ConsoleInputGuard() = default;
    ConsoleInputGuard(const ConsoleInputGuard&) = delete;
    ConsoleInputGuard& operator=(const ConsoleInputGuard&) = delete;

    [[nodiscard]] HANDLE input() const noexcept {
        return input_;
    }

private:
    HANDLE input_ = INVALID_HANDLE_VALUE;
    DWORD original_mode_ = 0;
    bool active_ = false;
};

struct ConsoleReadResult {
    bool succeeded = false;
    DWORD error_code = ERROR_SUCCESS;
    std::vector<wchar_t> commands;
};

struct ManageableScan {
    bool succeeded = false;
    DWORD error_code = ERROR_SUCCESS;
    std::vector<ChromeWindowSnapshot> windows;
};

[[nodiscard]] WindowIdentity MakeIdentity(
    const ChromeWindowSnapshot& snapshot) {
    return {
        .hwnd = snapshot.hwnd,
        .process_id = snapshot.process_id,
        .thread_id = snapshot.thread_id,
        .process_creation_time = snapshot.process_creation_time,
        .class_name = snapshot.class_name,
    };
}

[[nodiscard]] std::wstring FormatIdentity(
    const WindowIdentity& identity) {
    std::wostringstream output;
    output << L"HWND=0x" << std::uppercase << std::hex
           << reinterpret_cast<std::uintptr_t>(identity.hwnd)
           << std::dec << L", PID=" << identity.process_id
           << L", TID=" << identity.thread_id;
    return output.str();
}

void WriteLine(Logger* const logger,
               const std::wstring_view line,
               const bool error = false) {
    if (error) {
        std::wcerr << line << L'\n';
        if (logger != nullptr) {
            logger->Error(line);
        }
    } else {
        std::wcout << line << L'\n';
        if (logger != nullptr) {
            logger->Info(line);
        }
    }
}

[[nodiscard]] ManageableScan ScanManageableChromeWindows(
    Logger* const logger) {
    ManageableScan scan;
    const ChromeWindowEnumerationResult enumeration =
        EnumerateChromeWindows();
    if (!enumeration.succeeded) {
        scan.error_code = enumeration.error_code;
        WriteLine(
            logger,
            L"Chrome enumeration failed with Win32 error " +
                std::to_wstring(enumeration.error_code) + L'.',
            true);
        return scan;
    }
    for (const ChromeWindowRecord& record : enumeration.chrome_windows) {
        if (record.assessment.manageable) {
            scan.windows.push_back(record.snapshot);
        }
    }
    scan.succeeded = true;
    return scan;
}

[[nodiscard]] ConsoleReadResult ReadConsoleCommands(
    const HANDLE input) {
    ConsoleReadResult result;
    DWORD pending = 0;
    if (GetNumberOfConsoleInputEvents(input, &pending) == FALSE) {
        result.error_code = GetLastError();
        return result;
    }
    while (pending > 0) {
        std::array<INPUT_RECORD, 32> records{};
        DWORD read = 0;
        const DWORD requested =
            std::min(pending, static_cast<DWORD>(records.size()));
        if (ReadConsoleInputW(input, records.data(), requested, &read) ==
            FALSE) {
            result.error_code = GetLastError();
            return result;
        }
        for (DWORD index = 0; index < read; ++index) {
            const INPUT_RECORD& record = records[index];
            if (record.EventType != KEY_EVENT ||
                record.Event.KeyEvent.bKeyDown == FALSE) {
                continue;
            }
            const wchar_t command = static_cast<wchar_t>(std::towlower(
                record.Event.KeyEvent.uChar.UnicodeChar));
            if (command == L'p' || command == L'r' ||
                command == L'q' || command == L'h' || command == L'?') {
                result.commands.push_back(command);
            }
        }
        if (GetNumberOfConsoleInputEvents(input, &pending) == FALSE) {
            result.error_code = GetLastError();
            return result;
        }
    }
    result.succeeded = true;
    return result;
}

void PrintCommands(const bool paused,
                   const bool recovery_required) {
    std::wcout
        << L"\nPhase 1 commands (single key; Enter is not required):\n"
        << L"  p  Restore taskbar and original window layout, then pause\n"
        << L"  r  Retry an incomplete restoration\n"
        << L"  q  Restore safely and exit\n"
        << L"  h  Show these commands\n"
        << L"State: "
        << (recovery_required ? L"RECOVERY REQUIRED"
                              : paused ? L"PAUSED" : L"MANAGING")
        << L"\nThe close glyph is hit-test-only in Phase 1 and will not close Chrome.\n";
}

class V2ExperimentSession final : public ITabStripEventSink {
public:
    V2ExperimentSession(
        const HINSTANCE instance,
        Logger* const logger,
        std::filesystem::path recovery_path,
        std::vector<ChromeWindowSnapshot> windows)
        : instance_(instance),
          logger_(logger),
          windows_(std::move(windows)),
          recovery_journal_(std::move(recovery_path)),
          readiness_gate_(&model_),
          fixed_entry_manager_(
              &taskbar_controller_,
              &recovery_journal_,
              &readiness_gate_),
          activation_(&model_, &activation_gateway_) {}

    ~V2ExperimentSession() override {
        EmergencyCleanup();
    }

    V2ExperimentSession(const V2ExperimentSession&) = delete;
    V2ExperimentSession& operator=(const V2ExperimentSession&) = delete;

    [[nodiscard]] bool Prepare() {
        const TaskbarOperationResult taskbar_initialization =
            taskbar_controller_.InitializeTaskbarList();
        if (!taskbar_initialization.succeeded) {
            WriteLine(
                logger_,
                L"ITaskbarList initialization failed: " +
                    taskbar_initialization.message,
                true);
            return false;
        }

        const RecoveryLoadResult recovery = recovery_journal_.Load();
        if (!recovery.succeeded) {
            WriteLine(
                logger_,
                L"The V1 recovery journal is invalid or unreadable: " +
                    recovery.error_message,
                true);
            return false;
        }
        if (!recovery.states.empty()) {
            WriteLine(
                logger_,
                L"A previous taskbar recovery obligation exists. Run --restore-all before the V2 experiment.",
                true);
            return false;
        }

        std::vector<TabGroupCandidate> candidates;
        std::vector<TabStripItem> strip_items;
        identities_.reserve(windows_.size());
        candidates.reserve(windows_.size());
        strip_items.reserve(windows_.size());
        for (const ChromeWindowSnapshot& snapshot : windows_) {
            const WindowIdentity identity = MakeIdentity(snapshot);
            identities_.push_back(identity);
            candidates.push_back({
                .identity = identity,
                .title = snapshot.title,
            });
            strip_items.push_back({
                .identity = identity,
                .title = snapshot.title,
            });
        }
        active_identity_ = identities_.front();
        static_cast<void>(
            model_.Synchronize(candidates, active_identity_));

        const WindowCoordinationResult capture =
            placement_controller_.Capture(identities_);
        if (!capture.succeeded) {
            WriteLine(
                logger_,
                L"Original layout capture failed: " + capture.message,
                true);
            return false;
        }

        RECT anchor{};
        if (GetWindowRect(active_identity_.hwnd, &anchor) == FALSE) {
            WriteLine(
                logger_,
                L"Reading the initial group rectangle failed with Win32 error " +
                    std::to_wstring(GetLastError()) + L'.',
                true);
            return false;
        }
        geometry_ = CalculateWindowGroupGeometry(
            anchor, kV2TabStripHeight);
        if (!geometry_.valid) {
            WriteLine(
                logger_,
                L"The initial Chrome rectangle is too small for the Phase 1 tab strip.",
                true);
            return false;
        }

        DWORD strip_error = ERROR_SUCCESS;
        if (!tab_strip_.Create(
                instance_,
                active_identity_.hwnd,
                geometry_.tab_strip_bounds,
                strip_items,
                active_identity_,
                this,
                &strip_error)) {
            WriteLine(
                logger_,
                L"Creating the native tab strip failed with Win32 error " +
                    std::to_wstring(strip_error) + L'.',
                true);
            return false;
        }
        for (const WindowIdentity& identity : identities_) {
            if (!model_.MarkTabCreated(identity, true)) {
                WriteLine(
                    logger_,
                    L"TAB_CREATED rejected for " + FormatIdentity(identity),
                    true);
                return false;
            }
            WriteLine(
                logger_,
                L"ORDER 1 TAB_CREATED " + FormatIdentity(identity));
        }
        model_.SetTabStripHealthy(tab_strip_.IsHealthy());
        if (!model_.tab_strip_healthy()) {
            WriteLine(logger_, L"The native tab strip is not healthy.", true);
            return false;
        }

        const WindowCoordinationResult arranged =
            placement_controller_.Arrange(geometry_, active_identity_);
        if (!arranged.succeeded) {
            WriteLine(
                logger_,
                L"Applying the Phase 1 group rectangle failed: " +
                    arranged.message,
                true);
            return false;
        }
        WriteLine(logger_, L"ORDER 2 GROUP_LAYOUT_APPLIED");

        for (const WindowIdentity& identity : identities_) {
            const TabActivationReport verified = activation_.Verify(identity);
            if (!verified.succeeded) {
                WriteLine(
                    logger_,
                    L"Activation verification failed for " +
                        FormatIdentity(identity) + L": " + verified.message,
                    true);
                return false;
            }
            const TabActivationReport activated = activation_.Activate(identity);
            if (!activated.succeeded || !UpdateActiveVisual(identity)) {
                WriteLine(
                    logger_,
                    L"Activation-path exercise failed for " +
                        FormatIdentity(identity) + L": " + activated.message,
                    true);
                return false;
            }
            WriteLine(
                logger_,
                L"ORDER 3 ACTIVATION_VERIFIED " + FormatIdentity(identity));
        }
        const TabActivationReport initial_activation =
            activation_.Activate(active_identity_);
        if (!initial_activation.succeeded ||
            !UpdateActiveVisual(active_identity_)) {
            WriteLine(
                logger_,
                L"Restoring the initial active tab failed.",
                true);
            return false;
        }

        const FixedEntryReport taskbar = fixed_entry_manager_.Synchronize(
            windows_, active_identity_.hwnd);
        LogTaskbarReport(taskbar);
        if (!taskbar.succeeded) {
            WriteLine(
                logger_,
                L"The taskbar single-entry transaction failed: " +
                    taskbar.message,
                true);
            return false;
        }
        if (taskbar.operations.empty()) {
            WriteLine(
                logger_,
                L"One Chrome window needs no DeleteTab transaction.");
        }
        managing_ = true;
        return true;
    }

    [[nodiscard]] int Run(const HANDLE console_input) {
        PrintCommands(false, false);
        while (!quit_requested_) {
            const DWORD wait = MsgWaitForMultipleObjectsEx(
                1,
                &console_input,
                500,
                QS_ALLINPUT,
                MWMO_INPUTAVAILABLE);
            if (wait == WAIT_FAILED) {
                fatal_error_ =
                    L"The Phase 1 message wait failed with Win32 error " +
                    std::to_wstring(GetLastError()) + L'.';
            } else if (wait == WAIT_OBJECT_0) {
                const ConsoleReadResult input =
                    ReadConsoleCommands(console_input);
                if (!input.succeeded) {
                    fatal_error_ =
                        L"Reading Phase 1 console input failed with Win32 error " +
                        std::to_wstring(input.error_code) + L'.';
                } else {
                    for (const wchar_t command : input.commands) {
                        HandleCommand(command);
                        if (quit_requested_) {
                            break;
                        }
                    }
                }
            } else if (wait == WAIT_OBJECT_0 + 1) {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) !=
                       FALSE) {
                    if (message.message == WM_QUIT) {
                        fatal_error_ =
                            L"The Phase 1 UI message loop received WM_QUIT.";
                        break;
                    }
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }

            if (fatal_error_.empty() && managing_) {
                ValidateManagedIdentities();
            }
            if (!fatal_error_.empty()) {
                WriteLine(logger_, fatal_error_, true);
                fatal_exit_required_ = true;
                if (RestoreSession(L"Failure rollback")) {
                    quit_requested_ = true;
                    exit_code_ = 6;
                }
                fatal_error_.clear();
            }
        }
        return exit_code_;
    }

    void OnTabActivationRequested(
        const WindowIdentity& identity) override {
        if (!managing_ || recovery_required_) {
            return;
        }
        const TabActivationReport activated = activation_.Activate(identity);
        if (!activated.succeeded) {
            fatal_error_ =
                L"Tab activation failed for " + FormatIdentity(identity) +
                L": " + activated.message;
            return;
        }
        if (!UpdateActiveVisual(identity)) {
            fatal_error_ =
                L"The tab strip could not follow the activated window owner.";
            return;
        }
        WriteLine(
            logger_,
            L"TAB_ACTIVATED " + FormatIdentity(identity));
    }

    void OnTabCloseRequested(
        const WindowIdentity& identity) override {
        MessageBeep(MB_ICONINFORMATION);
        WriteLine(
            logger_,
            L"CLOSE_HIT_TEST_ONLY " + FormatIdentity(identity) +
                L"; Phase 1 did not close Chrome.");
    }

private:
    [[nodiscard]] bool UpdateActiveVisual(
        const WindowIdentity& identity) {
        DWORD owner_error = ERROR_SUCCESS;
        if (!tab_strip_.SetOwner(identity.hwnd, &owner_error) ||
            !tab_strip_.SetActive(identity)) {
            WriteLine(
                logger_,
                L"Updating the active tab-strip owner failed with Win32 error " +
                    std::to_wstring(owner_error) + L'.',
                true);
            return false;
        }
        active_identity_ = identity;
        return true;
    }

    void LogTaskbarReport(const FixedEntryReport& report) {
        for (const FixedEntryOperation& operation : report.operations) {
            const std::wstring operation_name =
                operation.kind == FixedEntryOperationKind::Remove
                    ? L"DELETE_TAB"
                    : L"ADD_TAB";
            WriteLine(
                logger_,
                operation_name + L" " + FormatIdentity(operation.identity) +
                    L" result=" +
                    (operation.result.succeeded ? L"SUCCESS" : L"FAIL") +
                    L" hresult=" +
                    std::to_wstring(operation.result.hresult) +
                    L" win32=" +
                    std::to_wstring(operation.result.win32_error));
            if (operation.kind == FixedEntryOperationKind::Remove &&
                operation.result.succeeded) {
                WriteLine(
                    logger_,
                    L"ORDER 4 RECOVERY_WRITE_AND_READINESS_CONFIRMED; ORDER 5 DELETE_TAB_COMPLETED " +
                        FormatIdentity(operation.identity));
            }
        }
        if (!report.persistence_error.empty()) {
            WriteLine(
                logger_,
                L"Recovery persistence error: " + report.persistence_error,
                true);
        }
        if (!report.readiness_error.empty()) {
            WriteLine(
                logger_,
                L"Internal-tab readiness error: " + report.readiness_error,
                true);
        }
    }

    void ValidateManagedIdentities() {
        const ProcessPresenceResult windowtabs = QueryWindowTabsPresence();
        if (!windowtabs.query_succeeded || windowtabs.running) {
            fatal_error_ = windowtabs.running
                               ? L"WindowTabs started during the isolated experiment; Phase 1 must roll back."
                               : L"WindowTabs presence could no longer be checked; Phase 1 must roll back.";
            return;
        }
        if (!tab_strip_.IsHealthy()) {
            fatal_error_ = L"The native tab strip became unhealthy.";
            return;
        }
        for (const WindowIdentity& expected : identities_) {
            const WindowIdentityQueryResult current =
                QueryWindowIdentity(expected.hwnd);
            if (!current.succeeded ||
                !WindowIdentitiesMatch(expected, current.identity)) {
                fatal_error_ =
                    L"A managed Chrome identity closed or changed; Phase 1 must roll back.";
                return;
            }
        }
    }

    void HandleCommand(const wchar_t command) {
        std::wcout << L"\nphase1> " << command << L'\n';
        if (command == L'h' || command == L'?') {
            PrintCommands(paused_, recovery_required_);
            return;
        }
        if (command == L'p') {
            if (paused_ && !recovery_required_) {
                std::wcout << L"The experiment is already paused.\n";
                return;
            }
            static_cast<void>(RestoreSession(L"User pause"));
            PrintCommands(paused_, recovery_required_);
            return;
        }
        if (command == L'r') {
            if (!recovery_required_) {
                std::wcout << L"No incomplete restoration needs retrying.\n";
                return;
            }
            const bool restored = RestoreSession(L"Recovery retry");
            if (restored && fatal_exit_required_) {
                quit_requested_ = true;
                exit_code_ = 6;
            }
            PrintCommands(paused_, recovery_required_);
            return;
        }
        if (command == L'q') {
            if (!paused_ || recovery_required_) {
                if (!RestoreSession(L"Normal exit")) {
                    PrintCommands(paused_, recovery_required_);
                    return;
                }
            }
            quit_requested_ = true;
        }
    }

    [[nodiscard]] bool RestoreTaskbarWithRetry(
        const std::wstring_view label) {
        FixedEntryReport report = fixed_entry_manager_.RestoreAll();
        LogTaskbarReport(report);
        if (report.succeeded &&
            fixed_entry_manager_.removed_window_count() == 0) {
            return true;
        }
        WriteLine(
            logger_,
            std::wstring(label) +
                L": first AddTab restoration failed; retrying once.",
            true);
        report = fixed_entry_manager_.RestoreAll();
        LogTaskbarReport(report);
        return report.succeeded &&
               fixed_entry_manager_.removed_window_count() == 0;
    }

    [[nodiscard]] bool RestoreLayoutWithRetry(
        const std::wstring_view label) {
        WindowGroupRestoreReport report =
            placement_controller_.RestoreAll();
        if (report.succeeded && !placement_controller_.needs_restore()) {
            return true;
        }
        WriteLine(
            logger_,
            std::wstring(label) +
                L": first window-layout restoration failed; retrying once.",
            true);
        report = placement_controller_.RestoreAll();
        return report.succeeded && !placement_controller_.needs_restore();
    }

    [[nodiscard]] bool RestoreSession(const std::wstring_view label) {
        managing_ = false;
        model_.SetTabStripHealthy(false);
        WriteLine(
            logger_,
            std::wstring(label) +
                L": stop new DeleteTab requests and restore taskbar entries.");
        if (!RestoreTaskbarWithRetry(label)) {
            recovery_required_ = true;
            model_.SetTabStripHealthy(tab_strip_.IsHealthy());
            WriteLine(
                logger_,
                L"ADD_TAB restoration is incomplete. The tab strip remains available; press r to retry.",
                true);
            return false;
        }
        if (!RestoreLayoutWithRetry(label)) {
            recovery_required_ = true;
            model_.SetTabStripHealthy(tab_strip_.IsHealthy());
            WriteLine(
                logger_,
                L"Window-layout restoration is incomplete. The tab strip remains available; press r to retry.",
                true);
            return false;
        }
        tab_strip_.Destroy();
        for (const WindowIdentity& identity : identities_) {
            static_cast<void>(model_.MarkTabCreated(identity, false));
        }
        recovery_required_ = false;
        paused_ = true;
        WriteLine(
            logger_,
            std::wstring(label) +
                L": taskbar entries and original window rectangles are restored; the tab strip is closed.");
        return true;
    }

    void EmergencyCleanup() noexcept {
        try {
            if (fixed_entry_manager_.removed_window_count() > 0) {
                static_cast<void>(RestoreTaskbarWithRetry(
                    L"Scope-exit emergency restoration"));
            }
            if (placement_controller_.needs_restore()) {
                static_cast<void>(RestoreLayoutWithRetry(
                    L"Scope-exit emergency restoration"));
            }
            tab_strip_.Destroy();
        } catch (...) {
            if (logger_ != nullptr) {
                logger_->Error(
                    L"The Phase 1 emergency cleanup raised an unexpected exception.");
            }
        }
    }

    HINSTANCE instance_ = nullptr;
    Logger* logger_ = nullptr;
    std::vector<ChromeWindowSnapshot> windows_;
    std::vector<WindowIdentity> identities_;
    RecoveryJournal recovery_journal_;
    TabGroupModel model_;
    TabGroupTaskbarReadinessGate readiness_gate_;
    TaskbarController taskbar_controller_;
    FixedEntryManager fixed_entry_manager_;
    Win32WindowActivationGateway activation_gateway_;
    TabActivationCoordinator activation_;
    WindowGroupPlacementController placement_controller_;
    TabStripWindow tab_strip_;
    WindowGroupGeometry geometry_;
    WindowIdentity active_identity_;
    bool managing_ = false;
    bool paused_ = false;
    bool recovery_required_ = false;
    bool fatal_exit_required_ = false;
    bool quit_requested_ = false;
    int exit_code_ = 0;
    std::wstring fatal_error_;
};

[[nodiscard]] int RunV2ExperimentImpl(
    const HINSTANCE instance,
    Logger* const logger,
    const std::filesystem::path& recovery_journal_path) {
    std::wcout
        << L"=== ChromeTaskbarMerger V2 Phase 1 isolated experiment ===\n"
        << L"This is not the default V1 tray behavior. It creates a minimal native tab strip,\n"
        << L"temporarily groups 1-5 normal Chrome windows, and keeps one taskbar entry.\n"
        << L"It never uses SetParent and does not modify Chrome data.\n\n";

    const ProcessPresenceResult windowtabs = QueryWindowTabsPresence();
    if (!windowtabs.query_succeeded) {
        WriteLine(
            logger,
            L"WindowTabs presence could not be checked; the experiment is blocked for safety.",
            true);
        return 4;
    }
    if (windowtabs.running) {
        WriteLine(
            logger,
            L"WindowTabs is running. Exit it completely before this isolated experiment.",
            true);
        return 4;
    }

    ManageableScan scan = ScanManageableChromeWindows(logger);
    if (!scan.succeeded) {
        return 3;
    }
    if (scan.windows.empty() || scan.windows.size() > 5) {
        WriteLine(
            logger,
            L"Phase 1 requires 1-5 manageable Chrome windows; found " +
                std::to_wstring(scan.windows.size()) + L'.',
            true);
        return 4;
    }

    std::wcout << L"Manageable Chrome windows: " << scan.windows.size()
               << L"\n";
    for (std::size_t index = 0; index < scan.windows.size(); ++index) {
        std::wcout << L"  " << (index + 1) << L". "
                   << scan.windows[index].title << L" ("
                   << FormatIdentity(MakeIdentity(scan.windows[index]))
                   << L")\n";
    }
    std::wcout
        << L"\nBefore continuing, make every listed Chrome window a normal floating window\n"
        << L"(not minimized, maximized, or snapped).\n"
        << L"During the experiment use only p/r/q; do not close the console or kill the process.\n"
        << L"Type V2-START to create the reversible group: " << std::flush;
    std::wstring confirmation;
    if (!std::getline(std::wcin, confirmation) ||
        confirmation != L"V2-START") {
        std::wcout << L"Experiment cancelled before any window or taskbar change.\n";
        return 0;
    }

    const ProcessPresenceResult final_windowtabs_check =
        QueryWindowTabsPresence();
    if (!final_windowtabs_check.query_succeeded ||
        final_windowtabs_check.running) {
        WriteLine(
            logger,
            L"WindowTabs started or became unverifiable before confirmation completed; no experiment change was made.",
            true);
        return 4;
    }

    ConsoleControlGuard control_guard;
    if (!control_guard.Activate()) {
        WriteLine(
            logger,
            L"Installing the Ctrl+C safety guard failed; no experiment change was made.",
            true);
        return 4;
    }
    ConsoleInputGuard input_guard;
    DWORD input_error = ERROR_SUCCESS;
    if (!input_guard.Activate(&input_error)) {
        WriteLine(
            logger,
            L"Enabling the reversible command loop failed with Win32 error " +
                std::to_wstring(input_error) +
                L"; no experiment change was made.",
            true);
        return 4;
    }

    V2ExperimentSession session(
        instance,
        logger,
        recovery_journal_path,
        std::move(scan.windows));
    if (!session.Prepare()) {
        WriteLine(
            logger,
            L"Phase 1 preparation failed; scope-exit restoration was attempted.",
            true);
        return 5;
    }
    WriteLine(
        logger,
        L"Phase 1 is managing: the native tabs are ready and the taskbar transaction completed.");
    return session.Run(input_guard.input());
}

}  // namespace

int RunV2Experiment(
    const HINSTANCE instance,
    Logger* const logger,
    const std::filesystem::path& recovery_journal_path) {
    try {
        return RunV2ExperimentImpl(instance, logger, recovery_journal_path);
    } catch (const std::exception& exception) {
        if (logger != nullptr) {
            logger->Error(
                std::string("Unhandled V2 Phase 1 exception: ") +
                exception.what());
        }
        std::wcerr
            << L"The V2 Phase 1 experiment stopped after an exception; emergency restoration was attempted.\n";
        return 6;
    } catch (...) {
        if (logger != nullptr) {
            logger->Error(L"Unhandled unknown V2 Phase 1 exception.");
        }
        std::wcerr
            << L"The V2 Phase 1 experiment stopped after an unknown exception; emergency restoration was attempted.\n";
        return 6;
    }
}

}  // namespace ctm
