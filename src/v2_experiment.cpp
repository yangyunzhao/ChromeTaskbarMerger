#include "v2_experiment.h"

#include "chrome_window.h"
#include "chrome_window_registry.h"
#include "fixed_entry_manager.h"
#include "group_recovery_journal.h"
#include "lifecycle_sync.h"
#include "recovery_journal.h"
#include "restore_command.h"
#include "tab_activation.h"
#include "tab_group_model.h"
#include "tab_strip_window.h"
#include "taskbar_controller.h"
#include "v2_taskbar_readiness.h"
#include "window_coordinator.h"
#include "window_identity_query.h"
#include "windowtabs_presence.h"
#include "win_event_monitor.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cwctype>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctm {
namespace {

constexpr UINT kV2LifecycleEventMessage = WM_APP + 0x42;
constexpr std::chrono::milliseconds kV2EventDebounce{100};
constexpr std::chrono::milliseconds kV2MaximumEventDelay{500};
constexpr std::chrono::milliseconds kV2FallbackScan{2000};
constexpr std::chrono::milliseconds kV2TransientWindowRetry{250};
constexpr std::chrono::milliseconds kV2GeometryDebounce{16};
constexpr std::chrono::milliseconds kV2GeometryMaximumDelay{32};

enum class GroupViewState {
    Normal,
    Minimized,
    Maximized,
    Fullscreen,
};

struct MonitorGeometry {
    bool succeeded = false;
    RECT monitor_bounds{};
    RECT work_area{};
};

[[nodiscard]] MonitorGeometry QueryMonitorGeometry(const HWND hwnd) noexcept {
    MonitorGeometry result;
    const HMONITOR monitor =
        MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr) {
        return result;
    }
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) == FALSE) {
        return result;
    }
    result.succeeded = true;
    result.monitor_bounds = info.rcMonitor;
    result.work_area = info.rcWork;
    return result;
}

[[nodiscard]] int TabStripHeightForWindow(const HWND hwnd) noexcept {
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) {
        dpi = USER_DEFAULT_SCREEN_DPI;
    }
    return ScalePixelsForDpi(kV2TabStripHeight, dpi);
}

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
    return MakeWindowIdentity(snapshot);
}

[[nodiscard]] bool WriteNativeConsole(
    const HANDLE output,
    const std::wstring_view text) noexcept {
    if (output == nullptr || output == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD mode = 0;
    if (GetConsoleMode(output, &mode) == FALSE) {
        return false;
    }

    std::size_t offset = 0;
    while (offset < text.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            text.size() - offset, 16'384));
        DWORD written = 0;
        if (WriteConsoleW(
                output,
                text.data() + offset,
                chunk,
                &written,
                nullptr) == FALSE ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

[[nodiscard]] bool WriteRedirectedUtf8(
    const HANDLE output,
    const std::wstring_view text) {
    if (output == nullptr || output == INVALID_HANDLE_VALUE || text.empty()) {
        return text.empty();
    }
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return false;
    }
    std::string encoded(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            encoded.data(),
            required,
            nullptr,
            nullptr) != required) {
        return false;
    }

    std::size_t offset = 0;
    while (offset < encoded.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            encoded.size() - offset, 16'384));
        DWORD written = 0;
        if (WriteFile(
                output,
                encoded.data() + offset,
                chunk,
                &written,
                nullptr) == FALSE ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

void WriteConsoleText(const std::wstring_view text,
                      const bool error,
                      const bool append_newline) {
    std::wstring output(text);
    if (append_newline) {
        output.push_back(L'\n');
    }
    const HANDLE handle = GetStdHandle(
        error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (WriteNativeConsole(handle, output) ||
        WriteRedirectedUtf8(handle, output)) {
        return;
    }

    std::wostream& fallback = error ? std::wcerr : std::wcout;
    fallback.clear();
    fallback.write(output.data(), static_cast<std::streamsize>(output.size()));
    fallback.flush();
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
    WriteConsoleText(line, error, true);
    if (logger != nullptr) {
        if (error) {
            logger->Error(line);
        } else {
            logger->Info(line);
        }
    }
}

void WritePrompt(Logger* const logger,
                 const std::wstring_view prompt) {
    WriteConsoleText(prompt, false, false);
    if (logger != nullptr) {
        logger->Info(prompt);
    }
}

[[nodiscard]] std::wstring_view WindowShowState(
    const HWND hwnd) noexcept {
    if (IsIconic(hwnd) != FALSE) {
        return L"minimized";
    }
    if (IsZoomed(hwnd) != FALSE) {
        return L"maximized";
    }
    return L"normal";
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

void PrintCommands(Logger* const logger,
                   const bool paused,
                   const bool recovery_required) {
    std::wostringstream output;
    output
        << L"\nPhase 4 commands (single key; Enter is not required):\n"
        << L"  p  Restore taskbar and original window layout, then pause\n"
        << L"  r  Retry an incomplete restoration\n"
        << L"  q  Restore safely and exit\n"
        << L"  h  Show these commands\n"
        << L"State: "
        << (recovery_required ? L"RECOVERY REQUIRED"
                              : paused ? L"PAUSED" : L"MANAGING")
        << L"\nThe close glyph requests WM_CLOSE for that exact Chrome identity.";
    WriteLine(logger, output.str());
}

class V2ExperimentSession final : public ITabStripEventSink {
public:
    V2ExperimentSession(
        const HINSTANCE instance,
        Logger* const logger,
        std::filesystem::path v1_recovery_path,
        std::filesystem::path group_recovery_path,
        std::vector<ChromeWindowSnapshot> windows)
        : instance_(instance),
          logger_(logger),
          windows_(std::move(windows)),
          v1_recovery_journal_(std::move(v1_recovery_path)),
          group_recovery_journal_(std::move(group_recovery_path)),
          readiness_gate_(&model_),
          fixed_entry_manager_(
              &taskbar_controller_,
              &group_recovery_journal_,
              &readiness_gate_),
          activation_(&model_, &activation_gateway_),
          lifecycle_schedule_(
              kV2EventDebounce,
              kV2MaximumEventDelay,
              kV2FallbackScan),
          geometry_schedule_(
              kV2GeometryDebounce,
              kV2GeometryMaximumDelay) {}

    ~V2ExperimentSession() override {
        win_event_monitor_.Stop();
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

        const RecoveryLoadResult recovery = v1_recovery_journal_.Load();
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

        GroupRecoveryLoadResult group_recovery =
            group_recovery_journal_.Load();
        if (!group_recovery.succeeded) {
            WriteLine(
                logger_,
                L"The V2 group recovery journal is invalid or unreadable: " +
                    group_recovery.error_message,
                true);
            return false;
        }
        std::wstring group_adopt_error;
        if (!group_recovery_journal_.Adopt(
                std::move(group_recovery.state), &group_adopt_error) ||
            group_recovery_journal_.state().HasObligations()) {
            WriteLine(
                logger_,
                group_adopt_error.empty()
                    ? L"A previous V2 group recovery obligation still exists; management is blocked."
                    : L"Adopting the V2 group recovery journal failed: " +
                          group_adopt_error,
                true);
            return false;
        }

        MSG queue_probe{};
        static_cast<void>(PeekMessageW(
            &queue_probe, nullptr, WM_USER, WM_USER, PM_NOREMOVE));
        DWORD monitor_error = ERROR_SUCCESS;
        if (!win_event_monitor_.Start(
                GetCurrentThreadId(),
                kV2LifecycleEventMessage,
                true,
                &monitor_error)) {
            WriteLine(
                logger_,
                L"Installing the out-of-context WinEvent monitor failed with Win32 error " +
                    std::to_wstring(monitor_error) + L'.',
                true);
            return false;
        }

        const ChromeWindowRegistryReport registry_report =
            registry_.Synchronize(windows_, GetForegroundWindow());
        if (!registry_report.succeeded ||
            !registry_.active_identity().has_value()) {
            WriteLine(
                logger_,
                L"The initial Chrome window registry synchronization failed.",
                true);
            return false;
        }
        windows_.assign(
            registry_.windows().begin(), registry_.windows().end());

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
        active_identity_ = *registry_.active_identity();
        static_cast<void>(
            model_.Synchronize(candidates, active_identity_));

        const WindowCoordinationResult capture =
            placement_controller_.Capture(identities_);
        if (!capture.succeeded) {
            WriteLine(
                logger_,
                L"Original layout capture failed for " +
                    FormatIdentity(capture.identity) + L": " +
                    capture.message,
                true);
            return false;
        }
        std::wstring group_write_error;
        if (!group_recovery_journal_.BeginSession(
                identities_, &group_write_error)) {
            WriteLine(
                logger_,
                L"Persisting the Phase 4 group recovery intent failed before any layout mutation: " +
                    group_write_error,
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
            anchor, TabStripHeightForWindow(active_identity_.hwnd));
        if (!geometry_.valid) {
            WriteLine(
                logger_,
                L"The initial Chrome rectangle is too small for the V2 tab strip.",
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
        if (!group_recovery_journal_.MarkTabStripCreated(
                true, &group_write_error)) {
            WriteLine(
                logger_,
                L"Persisting tab-strip creation failed: " +
                    group_write_error,
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
        if (!group_recovery_journal_.MarkTabsCreated(
                identities_, true, &group_write_error)) {
            WriteLine(
                logger_,
                L"Persisting tab creation completion failed: " +
                    group_write_error,
                true);
            return false;
        }
        model_.SetTabStripHealthy(tab_strip_.IsHealthy());
        if (!model_.tab_strip_healthy()) {
            WriteLine(logger_, L"The native tab strip is not healthy.", true);
            return false;
        }

        if (!group_recovery_journal_.PlanLayoutMutation(
                identities_, &group_write_error)) {
            WriteLine(
                logger_,
                L"Persisting the layout write-ahead failed; no group move was attempted: " +
                    group_write_error,
                true);
            return false;
        }
        const WindowCoordinationResult arranged =
            placement_controller_.Arrange(geometry_, active_identity_);
        if (!arranged.succeeded) {
            WriteLine(
                logger_,
                L"Applying the V2 group rectangle failed: " +
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
        lifecycle_schedule_.MarkSynchronized(
            LifecycleSyncSchedule::Clock::now());
        managing_ = true;
        return true;
    }

    [[nodiscard]] int Run(const HANDLE console_input) {
        PrintCommands(logger_, false, false);
        while (!quit_requested_) {
            const auto before_wait = LifecycleSyncSchedule::Clock::now();
            auto scheduled_delay = managing_
                                       ? lifecycle_schedule_.DelayUntilDue(
                                             before_wait)
                                       : std::chrono::milliseconds(2000);
            if (managing_ && geometry_schedule_.pending()) {
                scheduled_delay = std::min(
                    scheduled_delay,
                    geometry_schedule_.DelayUntilDue(before_wait));
            }
            const DWORD wait_timeout = static_cast<DWORD>(std::clamp<
                long long>(scheduled_delay.count(), 0, 2000));
            const DWORD wait = MsgWaitForMultipleObjectsEx(
                1,
                &console_input,
                wait_timeout,
                QS_ALLINPUT,
                MWMO_INPUTAVAILABLE);
            if (wait == WAIT_FAILED) {
                fatal_error_ =
                    L"The Phase 4 message wait failed with Win32 error " +
                    std::to_wstring(GetLastError()) + L'.';
            } else if (wait == WAIT_OBJECT_0) {
                const ConsoleReadResult input =
                    ReadConsoleCommands(console_input);
                if (!input.succeeded) {
                    fatal_error_ =
                        L"Reading Phase 4 console input failed with Win32 error " +
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
                            L"The Phase 4 UI message loop received WM_QUIT.";
                        break;
                    }
                    if (message.hwnd == nullptr &&
                        message.message == kV2LifecycleEventMessage) {
                        const ChromeWindowEvent event =
                            ChromeWinEventMonitor::DecodeMessage(
                                message.wParam, message.lParam);
                        if (RegistryContainsHandle(event.hwnd) &&
                            (event.kind ==
                                 ChromeWindowEventKind::LocationChanged ||
                             event.kind ==
                                 ChromeWindowEventKind::MinimizeStarted ||
                             event.kind ==
                                 ChromeWindowEventKind::MinimizeEnded)) {
                            geometry_schedule_.RecordEvent(
                                event,
                                GeometrySyncSchedule::Clock::now());
                            continue;
                        }
                        if (ShouldRecordLifecycleEvent(event)) {
                            lifecycle_schedule_.RecordEvent(
                                event,
                                LifecycleSyncSchedule::Clock::now());
                        }
                        continue;
                    }
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }

            if (fatal_error_.empty() && managing_) {
                const auto now = LifecycleSyncSchedule::Clock::now();
                const LifecycleSyncReason reason =
                    lifecycle_schedule_.DueReason(now);
                if (reason != LifecycleSyncReason::None &&
                    SynchronizeLifecycle(reason)) {
                    lifecycle_schedule_.MarkSynchronized(now);
                }
            }
            if (fatal_error_.empty() && managing_) {
                const auto now = GeometrySyncSchedule::Clock::now();
                if (geometry_schedule_.IsDue(now) &&
                    SynchronizeGroupGeometry()) {
                    geometry_schedule_.MarkSynchronized();
                }
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
        if (!managing_ || recovery_required_) {
            return;
        }
        const WindowIdentityQueryResult current =
            QueryWindowIdentity(identity.hwnd);
        if (!current.succeeded ||
            !WindowIdentitiesMatch(identity, current.identity)) {
            lifecycle_schedule_.RecordEvent(
                {.kind = ChromeWindowEventKind::Destroyed,
                 .hwnd = identity.hwnd},
                LifecycleSyncSchedule::Clock::now());
            return;
        }
        if (PostMessageW(identity.hwnd, WM_CLOSE, 0, 0) == FALSE) {
            fatal_error_ =
                L"Posting WM_CLOSE to the selected Chrome identity failed with Win32 error " +
                std::to_wstring(GetLastError()) + L'.';
            return;
        }
        WriteLine(
            logger_,
            L"TAB_CLOSE_REQUESTED " + FormatIdentity(identity));
        lifecycle_schedule_.RecordEvent(
            {.kind = ChromeWindowEventKind::NameChanged,
             .hwnd = identity.hwnd},
            LifecycleSyncSchedule::Clock::now());
    }

private:
    [[nodiscard]] std::optional<WindowIdentity> FindRegistryIdentity(
        const HWND hwnd) const {
        const auto match = std::find_if(
            registry_.windows().begin(),
            registry_.windows().end(),
            [hwnd](const ChromeWindowSnapshot& snapshot) {
                return snapshot.hwnd == hwnd;
            });
        if (match == registry_.windows().end()) {
            return std::nullopt;
        }
        return MakeIdentity(*match);
    }

    [[nodiscard]] std::optional<WindowIdentity> GeometryDriver() const {
        for (const HWND hint : {
                 geometry_schedule_.minimize_ended_hint(),
                 geometry_schedule_.minimize_started_hint(),
                 geometry_schedule_.location_hint(),
                 GetForegroundWindow(),
                 active_identity_.hwnd}) {
            if (const auto identity = FindRegistryIdentity(hint);
                identity.has_value()) {
                return identity;
            }
        }
        return std::nullopt;
    }

    void ApplyShowCommandToGroup(const int show_command) const noexcept {
        for (const ChromeWindowSnapshot& snapshot : registry_.windows()) {
            static_cast<void>(
                ShowWindowAsync(snapshot.hwnd, show_command));
        }
    }

    [[nodiscard]] bool SetTabStripVisibility(const bool visible) {
        DWORD error = ERROR_SUCCESS;
        if (!tab_strip_.SetVisible(visible, &error)) {
            fatal_error_ =
                L"Changing the Phase 4 tab-strip visibility failed with Win32 error " +
                std::to_wstring(error) + L'.';
            return false;
        }
        return true;
    }

    [[nodiscard]] bool ApplyGroupGeometry(
        const WindowGroupGeometry& geometry,
        const WindowIdentity& active_identity,
        const std::wstring_view reason,
        const bool require_native_normal = false) {
        if (!geometry.valid) {
            fatal_error_ = L"Phase 4 calculated an invalid group geometry.";
            return false;
        }
        const WindowCoordinationResult arranged = require_native_normal
            ? placement_controller_.ArrangeAsNormal(geometry, active_identity)
            : placement_controller_.Arrange(geometry, active_identity);
        if (!arranged.succeeded) {
            fatal_error_ =
                L"Phase 4 group arrangement failed: " + arranged.message;
            return false;
        }
        DWORD bounds_error = ERROR_SUCCESS;
        if (!tab_strip_.SetBounds(
                geometry.tab_strip_bounds, &bounds_error)) {
            fatal_error_ =
                L"Moving the Phase 4 tab strip failed with Win32 error " +
                std::to_wstring(bounds_error) + L'.';
            return false;
        }
        if (model_.CanActivate(active_identity)) {
            static_cast<void>(model_.SetActive(active_identity));
            if (!UpdateActiveVisual(active_identity)) {
                fatal_error_ =
                    L"The Phase 4 tab strip could not follow the geometry driver.";
                return false;
            }
        }
        geometry_ = geometry;
        WriteLine(
            logger_,
            L"GROUP_GEOMETRY reason=" + std::wstring(reason) +
                L" left=" + std::to_wstring(geometry.group_bounds.left) +
                L" top=" + std::to_wstring(geometry.group_bounds.top) +
                L" right=" + std::to_wstring(geometry.group_bounds.right) +
                L" bottom=" +
                std::to_wstring(geometry.group_bounds.bottom));
        return true;
    }

    [[nodiscard]] bool SynchronizeGroupGeometry() {
        const std::optional<WindowIdentity> driver = GeometryDriver();
        if (!driver.has_value() || registry_.windows().empty()) {
            return true;
        }
        const WindowIdentityQueryResult current =
            QueryWindowIdentity(driver->hwnd);
        if (!current.succeeded ||
            !WindowIdentitiesMatch(*driver, current.identity)) {
            lifecycle_schedule_.RecordEvent(
                {.kind = ChromeWindowEventKind::Destroyed,
                 .hwnd = driver->hwnd},
                LifecycleSyncSchedule::Clock::now());
            return true;
        }

        RECT driver_bounds{};
        if (GetWindowRect(driver->hwnd, &driver_bounds) == FALSE) {
            fatal_error_ =
                L"Reading the Phase 4 geometry driver failed with Win32 error " +
                std::to_wstring(GetLastError()) + L'.';
            return false;
        }
        const MonitorGeometry monitor =
            QueryMonitorGeometry(driver->hwnd);
        if (!monitor.succeeded) {
            fatal_error_ = L"Reading the Phase 4 monitor geometry failed.";
            return false;
        }

        const bool minimize_started =
            geometry_schedule_.minimize_started_hint() != nullptr;
        const bool minimize_ended =
            geometry_schedule_.minimize_ended_hint() != nullptr;
        if (view_state_ != GroupViewState::Minimized &&
            (minimize_started || IsIconic(driver->hwnd) != FALSE)) {
            state_before_minimize_ = view_state_;
            geometry_before_minimize_ = geometry_;
            ApplyShowCommandToGroup(SW_MINIMIZE);
            if (!SetTabStripVisibility(false)) {
                return false;
            }
            view_state_ = GroupViewState::Minimized;
            WriteLine(logger_, L"GROUP_STATE MINIMIZED");
            return true;
        }
        if (view_state_ == GroupViewState::Minimized) {
            if (!minimize_ended || IsIconic(driver->hwnd) != FALSE) {
                return true;
            }
            view_state_ = state_before_minimize_;
            const bool restore_maximized =
                view_state_ == GroupViewState::Maximized;
            const WindowGroupGeometry restored_geometry =
                restore_maximized
                    ? CalculateWindowGroupGeometry(
                          monitor.work_area,
                          TabStripHeightForWindow(driver->hwnd))
                    : geometry_before_minimize_;
            if (!ApplyGroupGeometry(
                    restored_geometry,
                    *driver,
                    restore_maximized ? L"restore-maximized"
                                      : L"restore-normal",
                    true) ||
                !SetTabStripVisibility(true)) {
                return false;
            }
            WriteLine(
                logger_,
                restore_maximized ? L"GROUP_STATE MAXIMIZED"
                                  : L"GROUP_STATE NORMAL");
            return true;
        }

        const bool fullscreen = IsFullscreenRectangle(
            driver_bounds, monitor.monitor_bounds, 2);
        if (fullscreen) {
            if (view_state_ != GroupViewState::Fullscreen) {
                state_before_fullscreen_ = view_state_;
                geometry_before_fullscreen_ = geometry_;
                view_state_ = GroupViewState::Fullscreen;
                if (!SetTabStripVisibility(false)) {
                    return false;
                }
                WriteLine(logger_, L"GROUP_STATE FULLSCREEN");
            }
            return true;
        }
        if (view_state_ == GroupViewState::Fullscreen) {
            view_state_ = state_before_fullscreen_;
            if (view_state_ == GroupViewState::Maximized ||
                IsZoomed(driver->hwnd) != FALSE) {
                view_state_ = GroupViewState::Maximized;
                if (!ApplyGroupGeometry(
                        CalculateWindowGroupGeometry(
                            monitor.work_area,
                            TabStripHeightForWindow(driver->hwnd)),
                        *driver,
                        L"fullscreen-exit-maximized",
                        true) ||
                    !SetTabStripVisibility(true)) {
                    return false;
                }
                WriteLine(logger_, L"GROUP_STATE MAXIMIZED");
                return true;
            }
            view_state_ = GroupViewState::Normal;
            if (!ApplyGroupGeometry(
                    geometry_before_fullscreen_,
                    *driver,
                    L"fullscreen-exit-normal",
                    true) ||
                !SetTabStripVisibility(true)) {
                return false;
            }
            WriteLine(logger_, L"GROUP_STATE NORMAL");
            return true;
        }

        if (IsZoomed(driver->hwnd) != FALSE) {
            if (view_state_ == GroupViewState::Maximized) {
                view_state_ = GroupViewState::Normal;
                if (!ApplyGroupGeometry(
                        geometry_before_maximize_,
                        *driver,
                        L"managed-maximize-restore",
                        true) ||
                    !SetTabStripVisibility(true)) {
                    return false;
                }
                WriteLine(logger_, L"GROUP_STATE NORMAL");
                return true;
            }
            geometry_before_maximize_ = geometry_;
            view_state_ = GroupViewState::Maximized;
            if (!ApplyGroupGeometry(
                    CalculateWindowGroupGeometry(
                        monitor.work_area,
                        TabStripHeightForWindow(driver->hwnd)),
                    *driver,
                    L"managed-maximize",
                    true) ||
                !SetTabStripVisibility(true)) {
                return false;
            }
            WriteLine(logger_, L"GROUP_STATE MAXIMIZED");
            return true;
        }
        if (view_state_ == GroupViewState::Maximized) {
            if (RectanglesEqual(driver_bounds, geometry_.content_bounds)) {
                return true;
            }
            // A title-bar drag from the managed-maximized rectangle behaves
            // like native drag-to-restore and continues through normal
            // move/resize synchronization below.
            view_state_ = GroupViewState::Normal;
            WriteLine(logger_, L"GROUP_STATE NORMAL reason=title-drag");
        }

        const WindowGroupGeometry moved =
            CalculateWindowGroupGeometryFromContentBounds(
                driver_bounds,
                monitor.work_area,
                TabStripHeightForWindow(driver->hwnd));
        if (!moved.valid) {
            fatal_error_ =
                L"The moved Chrome rectangle cannot form a safe Phase 4 group.";
            return false;
        }
        if (RectanglesEqual(moved.group_bounds, geometry_.group_bounds)) {
            return true;
        }
        return ApplyGroupGeometry(moved, *driver, L"move-or-resize");
    }

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

    [[nodiscard]] bool RegistryContainsHandle(
        const HWND hwnd) const noexcept {
        return std::any_of(
            registry_.windows().begin(),
            registry_.windows().end(),
            [hwnd](const ChromeWindowSnapshot& snapshot) {
                return snapshot.hwnd == hwnd;
            });
    }

    [[nodiscard]] bool ShouldRecordLifecycleEvent(
        const ChromeWindowEvent& event) const {
        if (event.hwnd == nullptr) {
            return false;
        }
        if (RegistryContainsHandle(event.hwnd)) {
            return true;
        }
        switch (event.kind) {
            case ChromeWindowEventKind::Created:
            case ChromeWindowEventKind::Shown:
            case ChromeWindowEventKind::NameChanged:
            case ChromeWindowEventKind::Foreground:
                return IsManageableChromeWindow(event.hwnd);
            case ChromeWindowEventKind::Destroyed:
            case ChromeWindowEventKind::Hidden:
            case ChromeWindowEventKind::MinimizeStarted:
            case ChromeWindowEventKind::MinimizeEnded:
            case ChromeWindowEventKind::LocationChanged:
                return false;
        }
        return false;
    }

    [[nodiscard]] bool CheckLifecyclePrerequisites() {
        const ProcessPresenceResult windowtabs = QueryWindowTabsPresence();
        if (!windowtabs.query_succeeded || windowtabs.running) {
            fatal_error_ = windowtabs.running
                               ? L"WindowTabs started during the isolated experiment; Phase 4 must roll back."
                               : L"WindowTabs presence could no longer be checked; Phase 4 must roll back.";
            return false;
        }
        return true;
    }

    [[nodiscard]] std::vector<TabGroupCandidate>
    MakeRegistryCandidates() const {
        std::vector<TabGroupCandidate> candidates;
        candidates.reserve(registry_.windows().size());
        for (const ChromeWindowSnapshot& snapshot : registry_.windows()) {
            candidates.push_back({
                .identity = MakeIdentity(snapshot),
                .title = snapshot.title,
            });
        }
        return candidates;
    }

    [[nodiscard]] std::vector<TabStripItem> MakeRegistryStripItems() const {
        std::vector<TabStripItem> items;
        items.reserve(registry_.windows().size());
        for (const ChromeWindowSnapshot& snapshot : registry_.windows()) {
            items.push_back({
                .identity = MakeIdentity(snapshot),
                .title = snapshot.title,
            });
        }
        return items;
    }

    [[nodiscard]] std::vector<WindowIdentity>
    MakeRegistryIdentities() const {
        std::vector<WindowIdentity> identities;
        identities.reserve(registry_.windows().size());
        for (const ChromeWindowSnapshot& snapshot : registry_.windows()) {
            identities.push_back(MakeIdentity(snapshot));
        }
        return identities;
    }

    [[nodiscard]] bool ReconcileDestroyedHandleHints() {
        destroy_hint_membership_changed_ = false;
        const std::span<const HWND> destroyed =
            lifecycle_schedule_.destroyed_handles();
        if (destroyed.empty()) {
            return true;
        }
        const ChromeWindowRegistryReport invalidated =
            registry_.InvalidateHandles(destroyed);
        if (!invalidated.succeeded) {
            fatal_error_ =
                L"The Chrome registry rejected destroy-event reconciliation.";
            return false;
        }
        if (invalidated.removed_count == 0) {
            return true;
        }
        destroy_hint_membership_changed_ = true;

        static_cast<void>(
            placement_controller_.InvalidateHandles(destroyed));
        const std::vector<TabGroupCandidate> surviving_candidates =
            MakeRegistryCandidates();
        static_cast<void>(model_.Synchronize(
            surviving_candidates, registry_.active_identity()));
        model_.SetTabStripHealthy(tab_strip_.IsHealthy());
        windows_.assign(
            registry_.windows().begin(), registry_.windows().end());
        const FixedEntryReport taskbar = fixed_entry_manager_.Synchronize(
            windows_, GetForegroundWindow());
        LogTaskbarReport(taskbar);
        if (!taskbar.succeeded) {
            fatal_error_ =
                L"Restoring taskbar state for a destroyed HWND before reuse failed: " +
                taskbar.message;
            return false;
        }
        WriteLine(
            logger_,
            L"LIFECYCLE_DESTROY_HINT invalidated=" +
                std::to_wstring(invalidated.removed_count));
        return true;
    }

    [[nodiscard]] bool EnsureGroupGeometry(
        const WindowIdentity& active_identity,
        const bool force_reanchor) {
        if (geometry_.valid && !force_reanchor) {
            return true;
        }
        RECT anchor{};
        if (GetWindowRect(active_identity.hwnd, &anchor) == FALSE) {
            fatal_error_ =
                L"Reading the lifecycle group anchor failed with Win32 error " +
                std::to_wstring(GetLastError()) + L'.';
            return false;
        }
        geometry_ =
            CalculateWindowGroupGeometry(
                anchor, TabStripHeightForWindow(active_identity.hwnd));
        if (!geometry_.valid) {
            fatal_error_ =
                L"The current Chrome rectangle is too small for the V2 tab strip.";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool SynchronizeLifecycle(
        const LifecycleSyncReason reason) {
        if (!CheckLifecyclePrerequisites()) {
            return false;
        }

        ManageableScan scan = ScanManageableChromeWindows(logger_);
        if (!scan.succeeded) {
            fatal_error_ = L"The Phase 4 fallback Chrome scan failed.";
            return false;
        }
        if (scan.windows.size() > 5) {
            fatal_error_ =
                L"Phase 4 supports at most 5 manageable Chrome windows; the new window remains visible on the taskbar.";
            return false;
        }

        const auto unavailable = std::find_if(
            scan.windows.begin(),
            scan.windows.end(),
            [](const ChromeWindowSnapshot& snapshot) {
                return IsWindowEnabled(snapshot.hwnd) == FALSE;
            });
        if (unavailable != scan.windows.end()) {
            const WindowIdentity identity = MakeIdentity(*unavailable);
            if (!transient_wait_identity_.has_value() ||
                !WindowIdentitiesMatch(
                    *transient_wait_identity_, identity)) {
                WriteLine(
                    logger_,
                    L"LIFECYCLE_DEFER " + FormatIdentity(identity) +
                        L" is temporarily disabled; retrying without changing its taskbar or layout state.");
            }
            transient_wait_identity_ = identity;
            lifecycle_schedule_.DeferSynchronizationUntil(
                LifecycleSyncSchedule::Clock::now() +
                kV2TransientWindowRetry);
            return false;
        }
        if (transient_wait_identity_.has_value()) {
            WriteLine(
                logger_,
                L"LIFECYCLE_RESUME Chrome windows are ready; the deferred lifecycle transaction will continue.");
            transient_wait_identity_.reset();
        }

        if (!ReconcileDestroyedHandleHints()) {
            return false;
        }

        HWND foreground = GetForegroundWindow();
        if (lifecycle_schedule_.foreground_hint() != nullptr) {
            foreground = lifecycle_schedule_.foreground_hint();
        }
        const ChromeWindowRegistryReport registry_report =
            registry_.Synchronize(scan.windows, foreground);
        if (!registry_report.succeeded) {
            fatal_error_ =
                L"The Chrome registry rejected lifecycle synchronization.";
            return false;
        }
        windows_.assign(
            registry_.windows().begin(), registry_.windows().end());

        if (!registry_report.HasVisualChanges() &&
            !destroy_hint_membership_changed_) {
            return true;
        }

        const std::vector<WindowIdentity> current_identities =
            MakeRegistryIdentities();
        const bool membership_changed =
            registry_report.HasMembershipChanges() ||
            destroy_hint_membership_changed_;
        if (!registry_.windows().empty() &&
            !tab_strip_.IsHealthy() && !membership_changed) {
            fatal_error_ =
                L"The native tab strip became unhealthy without a Chrome lifecycle change.";
            return false;
        }
        if (membership_changed) {
            const WindowCoordinationResult participants =
                placement_controller_.SynchronizeParticipants(
                    current_identities);
            if (!participants.succeeded) {
                fatal_error_ =
                    L"Capturing the changed Chrome membership failed: " +
                    participants.message;
                return false;
            }
            std::wstring group_write_error;
            if (!group_recovery_journal_.EnsureMembers(
                    current_identities, &group_write_error)) {
                fatal_error_ =
                    L"Persisting changed Chrome membership failed before layout mutation: " +
                    group_write_error;
                return false;
            }
        }

        const std::vector<TabGroupCandidate> candidates =
            MakeRegistryCandidates();
        static_cast<void>(
            model_.Synchronize(candidates, registry_.active_identity()));

        if (registry_.windows().empty()) {
            model_.SetTabStripHealthy(false);
            tab_strip_.Destroy();
            std::wstring group_write_error;
            if (!group_recovery_journal_.MarkTabStripCreated(
                    false, &group_write_error)) {
                fatal_error_ =
                    L"Persisting tab-strip closure after all Chrome windows closed failed: " +
                    group_write_error;
                return false;
            }
            std::vector<WindowIdentity> removed_identities;
            removed_identities.reserve(registry_report.removed.size());
            for (const ChromeWindowSnapshot& removed :
                 registry_report.removed) {
                removed_identities.push_back(MakeIdentity(removed));
            }
            if (!removed_identities.empty() &&
                !group_recovery_journal_.MarkTabsCreated(
                    removed_identities, false, &group_write_error)) {
                fatal_error_ =
                    L"Persisting tab closure after all Chrome windows closed failed: " +
                    group_write_error;
                return false;
            }
            geometry_ = {};
            identities_.clear();
            active_identity_ = {};
            const FixedEntryReport taskbar =
                fixed_entry_manager_.Synchronize(
                    std::span<const ChromeWindowSnapshot>{}, nullptr);
            LogTaskbarReport(taskbar);
            if (!taskbar.succeeded) {
                fatal_error_ =
                    L"Cleaning taskbar state after all Chrome windows closed failed: " +
                    taskbar.message;
                return false;
            }
            WriteLine(logger_, L"LIFECYCLE_SYNC windows=0 tabs=0 taskbar=0");
            return true;
        }

        if (!registry_.active_identity().has_value()) {
            fatal_error_ =
                L"A non-empty Chrome registry has no active identity.";
            return false;
        }
        const WindowIdentity desired_active =
            *registry_.active_identity();
        const bool creating_strip = !tab_strip_.IsHealthy();
        if (!EnsureGroupGeometry(desired_active, creating_strip)) {
            return false;
        }

        const std::vector<TabStripItem> strip_items =
            MakeRegistryStripItems();
        DWORD strip_error = ERROR_SUCCESS;
        if (creating_strip) {
            tab_strip_.Destroy();
            if (!tab_strip_.Create(
                    instance_,
                    desired_active.hwnd,
                    geometry_.tab_strip_bounds,
                    strip_items,
                    desired_active,
                    this,
                    &strip_error)) {
                fatal_error_ =
                    L"Recreating the lifecycle tab strip failed with Win32 error " +
                    std::to_wstring(strip_error) + L'.';
                return false;
            }
            std::wstring group_write_error;
            if (!group_recovery_journal_.MarkTabStripCreated(
                    true, &group_write_error)) {
                fatal_error_ =
                    L"Persisting lifecycle tab-strip creation failed: " +
                    group_write_error;
                return false;
            }
        } else if (membership_changed ||
                   registry_report.updated_title_count != 0) {
            if (!tab_strip_.SetItems(
                    strip_items, desired_active, &strip_error)) {
                fatal_error_ =
                    L"Updating lifecycle tab items failed with Win32 error " +
                    std::to_wstring(strip_error) + L'.';
                return false;
            }
        }

        for (const ChromeWindowSnapshot& snapshot : registry_report.added) {
            const WindowIdentity identity = MakeIdentity(snapshot);
            if (!model_.MarkTabCreated(identity, true)) {
                fatal_error_ =
                    L"A new lifecycle tab could not be marked as created.";
                return false;
            }
        }
        if (creating_strip) {
            for (const WindowIdentity& identity : current_identities) {
                if (!model_.MarkTabCreated(identity, true)) {
                    fatal_error_ =
                        L"A recreated lifecycle tab could not be marked as created.";
                    return false;
                }
            }
        }
        std::vector<WindowIdentity> added_identities;
        added_identities.reserve(registry_report.added.size());
        for (const ChromeWindowSnapshot& added : registry_report.added) {
            added_identities.push_back(MakeIdentity(added));
        }
        std::vector<WindowIdentity> removed_identities;
        removed_identities.reserve(registry_report.removed.size());
        for (const ChromeWindowSnapshot& removed : registry_report.removed) {
            removed_identities.push_back(MakeIdentity(removed));
        }
        std::wstring group_write_error;
        if ((!added_identities.empty() &&
             !group_recovery_journal_.MarkTabsCreated(
                 added_identities, true, &group_write_error)) ||
            (!removed_identities.empty() &&
             !group_recovery_journal_.MarkTabsCreated(
                 removed_identities, false, &group_write_error))) {
            fatal_error_ =
                L"Persisting lifecycle tab completion failed: " +
                group_write_error;
            return false;
        }
        model_.SetTabStripHealthy(tab_strip_.IsHealthy());
        if (!model_.tab_strip_healthy()) {
            fatal_error_ = L"The lifecycle tab strip is not healthy.";
            return false;
        }

        if (membership_changed) {
            if (!group_recovery_journal_.PlanLayoutMutation(
                    current_identities, &group_write_error)) {
                fatal_error_ =
                    L"Persisting changed-group layout intent failed; no arrangement was attempted: " +
                    group_write_error;
                return false;
            }
            const WindowCoordinationResult arranged =
                placement_controller_.Arrange(geometry_, desired_active);
            if (!arranged.succeeded) {
                fatal_error_ =
                    L"Arranging the changed Chrome group failed: " +
                    arranged.message;
                return false;
            }
        }

        for (const ChromeWindowSnapshot& snapshot : registry_report.added) {
            const WindowIdentity identity = MakeIdentity(snapshot);
            const TabActivationReport verified = activation_.Verify(identity);
            if (!verified.succeeded) {
                fatal_error_ =
                    L"A new lifecycle tab failed activation verification: " +
                    verified.message;
                return false;
            }
        }
        if (creating_strip) {
            for (const WindowIdentity& identity : current_identities) {
                if (model_.CanActivate(identity)) {
                    continue;
                }
                const TabActivationReport verified =
                    activation_.Verify(identity);
                if (!verified.succeeded) {
                    fatal_error_ =
                        L"A recreated lifecycle tab failed activation verification: " +
                        verified.message;
                    return false;
                }
            }
        }

        if (!model_.SetActive(desired_active) ||
            !UpdateActiveVisual(desired_active)) {
            fatal_error_ =
                L"The lifecycle active tab could not follow the current Chrome foreground window.";
            return false;
        }
        identities_ = current_identities;

        if (membership_changed) {
            const FixedEntryReport taskbar =
                fixed_entry_manager_.Synchronize(
                    windows_, desired_active.hwnd);
            LogTaskbarReport(taskbar);
            if (!taskbar.succeeded) {
                fatal_error_ =
                    L"The lifecycle taskbar transaction failed: " +
                    taskbar.message;
                return false;
            }
        }

        const wchar_t* const reason_text =
            reason == LifecycleSyncReason::EventBatch
                ? L"event"
                : reason == LifecycleSyncReason::FallbackScan
                      ? L"fallback"
                      : L"initial";
        WriteLine(
            logger_,
            L"LIFECYCLE_SYNC reason=" + std::wstring(reason_text) +
                L" windows=" + std::to_wstring(windows_.size()) +
                L" added=" + std::to_wstring(registry_report.added_count) +
                L" removed=" +
                std::to_wstring(registry_report.removed_count) +
                L" titles=" +
                std::to_wstring(registry_report.updated_title_count));
        return true;
    }

    void HandleCommand(const wchar_t command) {
        WriteLine(
            logger_,
            L"phase4> " + std::wstring(1, command));
        if (command == L'h' || command == L'?') {
            PrintCommands(logger_, paused_, recovery_required_);
            return;
        }
        if (command == L'p') {
            if (paused_ && !recovery_required_) {
                WriteLine(logger_, L"The experiment is already paused.");
                return;
            }
            static_cast<void>(RestoreSession(L"User pause"));
            PrintCommands(logger_, paused_, recovery_required_);
            return;
        }
        if (command == L'r') {
            if (!recovery_required_) {
                WriteLine(
                    logger_,
                    L"No incomplete restoration needs retrying.");
                return;
            }
            const bool restored = RestoreSession(L"Recovery retry");
            if (restored && fatal_exit_required_) {
                quit_requested_ = true;
                exit_code_ = 6;
            }
            PrintCommands(logger_, paused_, recovery_required_);
            return;
        }
        if (command == L'q') {
            if (!paused_ || recovery_required_) {
                if (!RestoreSession(L"Normal exit")) {
                    PrintCommands(logger_, paused_, recovery_required_);
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
        const auto persist_controller_report =
            [this, label](const WindowGroupRestoreReport& report) {
                for (const WindowCoordinationResult& operation :
                     report.operations) {
                    if (!operation.succeeded) {
                        continue;
                    }
                    std::wstring persistence_error;
                    if (!group_recovery_journal_.MarkLayoutRestored(
                            operation.identity, &persistence_error)) {
                        WriteLine(
                            logger_,
                            std::wstring(label) +
                                L": persisting window-layout completion failed: " +
                                persistence_error,
                            true);
                        return false;
                    }
                }
                return true;
            };
        const auto journal_has_pending_layout = [this]() {
            return std::any_of(
                group_recovery_journal_.state().members.begin(),
                group_recovery_journal_.state().members.end(),
                [](const GroupMemberRecoveryState& member) {
                    return member.NeedsLayoutRestore();
                });
        };

        WindowGroupRestoreReport report =
            placement_controller_.RestoreAll();
        bool persisted = persist_controller_report(report);
        if (!report.succeeded || placement_controller_.needs_restore()) {
            WriteLine(
                logger_,
                std::wstring(label) +
                    L": first window-layout restoration failed; retrying once.",
                true);
            report = placement_controller_.RestoreAll();
            persisted = persist_controller_report(report) && persisted;
        }
        if (!report.succeeded || placement_controller_.needs_restore()) {
            return false;
        }
        if (!persisted || journal_has_pending_layout()) {
            Win32GroupRecoveryWindowGateway gateway;
            const GroupLayoutRecoveryReport persisted_report =
                RestorePersistedGroupLayouts(
                    &group_recovery_journal_, &gateway);
            for (const GroupLayoutRecoveryOperation& operation :
                 persisted_report.operations) {
                WriteLine(
                    logger_,
                    std::wstring(label) +
                        L" persisted layout " +
                        (operation.succeeded ? L"SUCCESS " : L"FAIL ") +
                        FormatIdentity(operation.identity) + L": " +
                        operation.message,
                    !operation.succeeded);
            }
            return persisted_report.succeeded &&
                   !journal_has_pending_layout();
        }
        return true;
    }

    [[nodiscard]] bool RestoreSession(const std::wstring_view label) {
        managing_ = false;
        win_event_monitor_.Stop();
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
        std::wstring clear_error;
        if (!group_recovery_journal_.Clear(&clear_error)) {
            recovery_required_ = true;
            WriteLine(
                logger_,
                std::wstring(label) +
                    L": taskbar and layout were restored and the tab strip was closed, but clearing the V2 recovery journal failed: " +
                    clear_error + L" Press r to retry cleanup.",
                true);
            return false;
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
            win_event_monitor_.Stop();
            bool taskbar_restored = true;
            if (fixed_entry_manager_.removed_window_count() > 0) {
                taskbar_restored = RestoreTaskbarWithRetry(
                    L"Scope-exit emergency restoration");
            }
            bool layout_restored = true;
            const bool persisted_layout_pending = std::any_of(
                group_recovery_journal_.state().members.begin(),
                group_recovery_journal_.state().members.end(),
                [](const GroupMemberRecoveryState& member) {
                    return member.NeedsLayoutRestore();
                });
            if (placement_controller_.needs_restore() ||
                persisted_layout_pending) {
                layout_restored = RestoreLayoutWithRetry(
                    L"Scope-exit emergency restoration");
            }
            if (taskbar_restored && layout_restored) {
                tab_strip_.Destroy();
                if (group_recovery_journal_.state().HasObligations()) {
                    std::wstring clear_error;
                    if (!group_recovery_journal_.Clear(&clear_error) &&
                        logger_ != nullptr) {
                        logger_->Error(
                            L"Scope-exit recovery succeeded, but clearing the V2 recovery journal failed: " +
                            clear_error);
                    }
                }
            }
        } catch (...) {
            if (logger_ != nullptr) {
                logger_->Error(
                    L"The Phase 4 emergency cleanup raised an unexpected exception.");
            }
        }
    }

    HINSTANCE instance_ = nullptr;
    Logger* logger_ = nullptr;
    std::vector<ChromeWindowSnapshot> windows_;
    std::vector<WindowIdentity> identities_;
    RecoveryJournal v1_recovery_journal_;
    GroupRecoveryJournal group_recovery_journal_;
    TabGroupModel model_;
    TabGroupTaskbarReadinessGate readiness_gate_;
    TaskbarController taskbar_controller_;
    FixedEntryManager fixed_entry_manager_;
    Win32WindowActivationGateway activation_gateway_;
    TabActivationCoordinator activation_;
    ChromeWindowRegistry registry_;
    LifecycleSyncSchedule lifecycle_schedule_;
    GeometrySyncSchedule geometry_schedule_;
    ChromeWinEventMonitor win_event_monitor_;
    WindowGroupPlacementController placement_controller_;
    TabStripWindow tab_strip_;
    WindowGroupGeometry geometry_;
    WindowGroupGeometry geometry_before_minimize_;
    WindowGroupGeometry geometry_before_maximize_;
    WindowGroupGeometry geometry_before_fullscreen_;
    WindowIdentity active_identity_;
    GroupViewState view_state_ = GroupViewState::Normal;
    GroupViewState state_before_minimize_ = GroupViewState::Normal;
    GroupViewState state_before_fullscreen_ = GroupViewState::Normal;
    bool managing_ = false;
    bool paused_ = false;
    bool recovery_required_ = false;
    bool fatal_exit_required_ = false;
    bool destroy_hint_membership_changed_ = false;
    bool quit_requested_ = false;
    int exit_code_ = 0;
    std::optional<WindowIdentity> transient_wait_identity_;
    std::wstring fatal_error_;
};

[[nodiscard]] int RunV2ExperimentImpl(
    const HINSTANCE instance,
    Logger* const logger,
    const std::filesystem::path& recovery_journal_path,
    const std::filesystem::path& group_recovery_journal_path) {
    std::wostringstream introduction;
    introduction
        << L"=== ChromeTaskbarMerger V2 Phase 4 isolated experiment ===\n"
        << L"This is not the default V1 tray behavior. It creates a minimal native tab strip,\n"
        << L"tracks 1-5 Chrome windows, synchronizes group geometry and display state, and keeps one taskbar entry.\n"
        << L"It never uses SetParent and does not modify Chrome data.";
    if (logger != nullptr && !logger->log_path().empty()) {
        introduction << L"\nLog file: " << logger->log_path().wstring();
    }
    introduction << L"\nV2 recovery journal: "
                 << group_recovery_journal_path.wstring();
    WriteLine(logger, introduction.str());

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

    const StartupGroupRecoveryResult startup_recovery =
        RestorePreviousGroupSession(logger, group_recovery_journal_path);
    if (!startup_recovery.succeeded) {
        WriteLine(
            logger,
            L"Phase 4 startup recovery blocked management: " +
                startup_recovery.error_message,
            true);
        return 5;
    }

    ManageableScan scan = ScanManageableChromeWindows(logger);
    if (!scan.succeeded) {
        return 3;
    }
    if (scan.windows.empty() || scan.windows.size() > 5) {
        WriteLine(
            logger,
            L"Phase 4 initially requires 1-5 manageable Chrome windows; found " +
                std::to_wstring(scan.windows.size()) + L'.',
            true);
        return 4;
    }

    bool all_windows_normal = true;
    std::wostringstream window_list;
    window_list << L"Manageable Chrome windows: " << scan.windows.size()
                << L'\n';
    for (std::size_t index = 0; index < scan.windows.size(); ++index) {
        const std::wstring_view state =
            WindowShowState(scan.windows[index].hwnd);
        all_windows_normal = all_windows_normal && state == L"normal";
        window_list << L"  " << (index + 1) << L". [" << state
                    << L"] " << scan.windows[index].title << L" ("
                    << FormatIdentity(MakeIdentity(scan.windows[index]))
                    << L")\n";
    }
    WriteLine(logger, window_list.str());
    if (!all_windows_normal) {
        WriteLine(
            logger,
            L"Preflight blocked: every listed Chrome window must show [normal]. Restore minimized or maximized windows, then rerun the experiment. No window or taskbar change was made.",
            true);
        return 4;
    }

    WriteLine(
        logger,
        L"Before continuing, keep every listed Chrome window normal and floating (not snapped).\nDuring the experiment you may create, close, reopen, rename, or activate Chrome windows.\nKeep at most five normal floating Chrome windows; do not close the console or kill the process.");
    WritePrompt(
        logger,
        L"Type V2-START to create the reversible group: ");
    std::wstring confirmation;
    if (!std::getline(std::wcin, confirmation) ||
        confirmation != L"V2-START") {
        WriteLine(
            logger,
            L"Experiment cancelled before any window or taskbar change.");
        return 0;
    }
    WriteLine(logger, L"V2-START confirmation accepted.");

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
        group_recovery_journal_path,
        std::move(scan.windows));
    if (!session.Prepare()) {
        WriteLine(
            logger,
            L"Phase 4 preparation failed; scope-exit restoration was attempted.",
            true);
        return 5;
    }
    WriteLine(
        logger,
        L"Phase 4 is managing: persistent group recovery, lifecycle events, geometry, native tabs, and the taskbar transaction are active.");
    return session.Run(input_guard.input());
}

}  // namespace

int RunV2Experiment(
    const HINSTANCE instance,
    Logger* const logger,
    const std::filesystem::path& recovery_journal_path,
    const std::filesystem::path& group_recovery_journal_path) {
    try {
        return RunV2ExperimentImpl(
            instance,
            logger,
            recovery_journal_path,
            group_recovery_journal_path);
    } catch (const std::exception& exception) {
        if (logger != nullptr) {
            logger->Error(
                std::string("Unhandled V2 Phase 4 exception: ") +
                exception.what());
        }
        WriteLine(
            logger,
            L"The V2 Phase 4 experiment stopped after an exception; emergency restoration was attempted.",
            true);
        return 6;
    } catch (...) {
        if (logger != nullptr) {
            logger->Error(L"Unhandled unknown V2 Phase 4 exception.");
        }
        WriteLine(
            logger,
            L"The V2 Phase 4 experiment stopped after an unknown exception; emergency restoration was attempted.",
            true);
        return 6;
    }
}

}  // namespace ctm
