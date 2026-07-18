#include "v2_experiment.h"

#include "auto_start.h"
#include "app_paths.h"
#include "chrome_window.h"
#include "chrome_window_registry.h"
#include "chrome_profile_resolver.h"
#include "ctm/version.h"
#include "fixed_entry_manager.h"
#include "group_recovery_journal.h"
#include "lifecycle_sync.h"
#include "management_state.h"
#include "recovery_journal.h"
#include "restore_command.h"
#include "tab_activation.h"
#include "tab_group_model.h"
#include "tab_name_store.h"
#include "tab_strip_window.h"
#include "taskbar_controller.h"
#include "tray_app.h"
#include "v2_taskbar_readiness.h"
#include "window_coordinator.h"
#include "window_identity_query.h"
#include "windowtabs_presence.h"
#include "win_event_monitor.h"

#include <Windows.h>
#include <CommCtrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cwctype>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
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
constexpr std::size_t kMaximumManagedChromeWindows = 5;

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
    return ScalePixelsForDpi(kV31TabStripHeight, dpi);
}

[[nodiscard]] int CaptionControlReserveForWindow(
    const HWND hwnd) noexcept {
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) {
        dpi = USER_DEFAULT_SCREEN_DPI;
    }
    return CalculateCaptionControlReserveWidth(
        GetSystemMetricsForDpi(SM_CXSIZE, dpi),
        GetSystemMetricsForDpi(SM_CYSIZE, dpi),
        GetSystemMetricsForDpi(SM_CXFRAME, dpi),
        GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi));
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

enum class V2ManagedPumpResult {
    Active,
    SafelyStopped,
    RecoveryRequired,
};

class V2ExperimentSession final : public ITabStripEventSink {
public:
    V2ExperimentSession(
        const HINSTANCE instance,
        Logger* const logger,
        std::filesystem::path v1_recovery_path,
        std::filesystem::path group_recovery_path,
        std::vector<ChromeWindowSnapshot> windows,
        AppConfig config = {},
        std::vector<TabNameRule> tab_name_rules = {},
        InMemoryTabNameStore* in_memory_tab_names = nullptr,
        ProfileTabNameStore* profile_tab_names = nullptr,
        ChromeProfileResolver* profile_resolver = nullptr,
        std::filesystem::path profile_tab_names_path = {},
        bool profile_name_persistence_available = false)
        : instance_(instance),
          logger_(logger),
          config_(config),
          tab_name_rules_(std::move(tab_name_rules)),
          in_memory_tab_names_(in_memory_tab_names),
          profile_tab_names_(profile_tab_names),
          profile_resolver_(profile_resolver),
          profile_tab_names_path_(std::move(profile_tab_names_path)),
          profile_name_persistence_available_(
              profile_name_persistence_available),
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
        const std::vector<std::wstring> display_names =
            ResolveDisplayNames(windows_);
        if (profile_name_persistence_available_ &&
            profile_resolver_ != nullptr) {
            for (std::size_t index = 0; index < windows_.size(); ++index) {
                const ChromeProfileResolution resolution =
                    profile_resolver_->Resolve(windows_[index]);
                WriteLine(
                    logger_,
                    L"PROFILE_NAME_RESOLUTION window_index=" +
                        std::to_wstring(index + 1U) + L" status=" +
                        std::wstring(ChromeProfileResolutionStatusName(
                            resolution.status)));
            }
        }
        for (std::size_t index = 0; index < windows_.size(); ++index) {
            const ChromeWindowSnapshot& snapshot = windows_[index];
            const WindowIdentity identity = MakeIdentity(snapshot);
            identities_.push_back(identity);
            candidates.push_back({
                .identity = identity,
                .title = display_names[index],
            });
            strip_items.push_back({
                .identity = identity,
                .title = display_names[index],
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
        ApplyConfiguredTabStrip(&geometry_);
        if (!geometry_.valid) {
            WriteLine(
                logger_,
                L"The initial Chrome rectangle is too small for the V2 tab strip.",
                true);
            return false;
        }

        DWORD strip_error = ERROR_SUCCESS;
        tab_strip_.SetMaximumTabWidth(config_.tab_width_pixels);
        tab_strip_.SetContentAlignment(config_.tab_strip_alignment);
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
                    if (ProcessThreadMessage(message)) {
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

    [[nodiscard]] bool ProcessThreadMessage(
        const MSG& message) {
        if (message.hwnd != nullptr ||
            message.message != kV2LifecycleEventMessage) {
            return false;
        }
        const ChromeWindowEvent event =
            ChromeWinEventMonitor::DecodeMessage(
                message.wParam, message.lParam);
        if (RegistryContainsHandle(event.hwnd) &&
            (event.kind == ChromeWindowEventKind::LocationChanged ||
             event.kind == ChromeWindowEventKind::MoveSizeEnded ||
             event.kind == ChromeWindowEventKind::MinimizeStarted ||
             event.kind == ChromeWindowEventKind::MinimizeEnded)) {
            geometry_schedule_.RecordEvent(
                event, GeometrySyncSchedule::Clock::now());
            return true;
        }
        if (ShouldRecordLifecycleEvent(event)) {
            lifecycle_schedule_.RecordEvent(
                event, LifecycleSyncSchedule::Clock::now());
        }
        return true;
    }

    [[nodiscard]] V2ManagedPumpResult PumpManagedWork() {
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
        if (fatal_error_.empty()) {
            return V2ManagedPumpResult::Active;
        }

        last_runtime_error_ = fatal_error_;
        WriteLine(logger_, fatal_error_, true);
        fatal_error_.clear();
        const bool restored = RestoreSession(
            L"Managed tray failure rollback");
        return restored ? V2ManagedPumpResult::SafelyStopped
                        : V2ManagedPumpResult::RecoveryRequired;
    }

    [[nodiscard]] bool PauseManaged(
        const std::wstring_view label) {
        return RestoreSession(label);
    }

    [[nodiscard]] bool RetryManagedRecovery(
        const std::wstring_view label) {
        return RestoreSession(label);
    }

    [[nodiscard]] bool RequestImmediateSynchronization() {
        if (!managing_) {
            return false;
        }
        const auto now = LifecycleSyncSchedule::Clock::now();
        const bool synchronized =
            SynchronizeLifecycle(LifecycleSyncReason::FallbackScan);
        if (synchronized) {
            lifecycle_schedule_.MarkSynchronized(now);
        }
        return synchronized;
    }

    [[nodiscard]] bool HandleTaskbarRecreated() {
        if (!managing_ || recovery_required_) {
            return false;
        }
        std::wstring reset_error;
        if (!fixed_entry_manager_.ResetAfterTaskbarRecreation(
                &reset_error)) {
            fatal_error_ =
                L"TaskbarCreated could not clear obsolete Shell recovery state: " +
                reset_error;
            return false;
        }
        taskbar_controller_.Shutdown();
        const TaskbarOperationResult initialized =
            taskbar_controller_.InitializeTaskbarList();
        if (!initialized.succeeded) {
            fatal_error_ =
                L"TaskbarCreated could not initialize the replacement taskbar COM object: " +
                initialized.message;
            return false;
        }
        const bool synchronized =
            SynchronizeLifecycle(LifecycleSyncReason::FallbackScan);
        if (!synchronized) {
            if (fatal_error_.empty()) {
                fatal_error_ =
                    L"TaskbarCreated could not rebuild the built-in managed state.";
            }
            return false;
        }
        lifecycle_schedule_.MarkSynchronized(
            LifecycleSyncSchedule::Clock::now());
        WriteLine(
            logger_,
            L"TASKBAR_CREATED_REBUILT tray=external taskbar_com=ready tabs=" +
                std::to_wstring(registry_.windows().size()));
        return true;
    }

    [[nodiscard]] bool ActivateRelativeTab(const int direction) {
        if (!managing_ || recovery_required_ || direction == 0 ||
            registry_.windows().empty()) {
            return false;
        }
        const std::optional<WindowIdentity> active =
            registry_.active_identity().has_value()
                ? registry_.active_identity()
                : std::optional<WindowIdentity>(active_identity_);
        if (!active.has_value()) {
            return false;
        }
        const auto current = std::find_if(
            registry_.windows().begin(),
            registry_.windows().end(),
            [&active](const ChromeWindowSnapshot& snapshot) {
                return WindowIdentitiesMatch(
                    MakeIdentity(snapshot), *active);
            });
        if (current == registry_.windows().end()) {
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(std::distance(
            registry_.windows().begin(), current));
        const std::size_t next = CalculateRelativeTabIndex(
            index, registry_.windows().size(), direction);
        const WindowIdentity target = MakeIdentity(
            registry_.windows()[next]);
        OnTabActivationRequested(target);
        return fatal_error_.empty();
    }

    [[nodiscard]] bool RequestDisplayEnvironmentSynchronization(
        const std::wstring_view reason) {
        if (!managing_ || recovery_required_ ||
            !registry_.active_identity().has_value()) {
            return false;
        }
        const WindowIdentity active = *registry_.active_identity();
        const auto now = LifecycleSyncSchedule::Clock::now();
        lifecycle_schedule_.RecordEvent(
            {.kind = ChromeWindowEventKind::NameChanged,
             .hwnd = active.hwnd},
            now);
        geometry_schedule_.RecordEvent(
            {.kind = ChromeWindowEventKind::LocationChanged,
             .hwnd = active.hwnd},
            now);
        WriteLine(
            logger_,
            L"DISPLAY_ENVIRONMENT_SYNC_SCHEDULED reason=" +
                std::wstring(reason));
        return true;
    }

    [[nodiscard]] bool managing() const noexcept {
        return managing_;
    }

    [[nodiscard]] bool recovery_required() const noexcept {
        return recovery_required_;
    }

    [[nodiscard]] std::size_t window_count() const noexcept {
        return registry_.windows().size();
    }

    [[nodiscard]] std::size_t ignored_window_count() const noexcept {
        return ignored_window_count_;
    }

    [[nodiscard]] const std::wstring& last_runtime_error() const noexcept {
        return last_runtime_error_;
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

    void OnTabNameChangeRequested(
        const WindowIdentity& identity,
        const std::wstring_view name) override {
        if (!managing_ || recovery_required_ ||
            in_memory_tab_names_ == nullptr) {
            return;
        }
        const WindowIdentityQueryResult current =
            QueryWindowIdentity(identity.hwnd);
        if (!current.succeeded ||
            !WindowIdentitiesMatch(identity, current.identity) ||
            !RegistryContainsHandle(identity.hwnd)) {
            lifecycle_schedule_.RecordEvent(
                {.kind = ChromeWindowEventKind::Destroyed,
                 .hwnd = identity.hwnd},
                LifecycleSyncSchedule::Clock::now());
            return;
        }
        const InMemoryTabNameUpdateResult updated =
            in_memory_tab_names_->Set(identity, name);
        if (updated != InMemoryTabNameUpdateResult::Stored &&
            updated != InMemoryTabNameUpdateResult::Cleared) {
            WriteLine(
                logger_,
                L"TAB_NAME_MEMORY_REJECTED " + FormatIdentity(identity),
                true);
            return;
        }
        ApplyProfileNameUpdate(identity, name);
        const std::optional<WindowIdentity> active =
            registry_.active_identity();
        if (!active.has_value()) {
            fatal_error_ =
                L"The in-memory tab-name update found no active Chrome identity.";
            return;
        }
        const std::vector<TabGroupCandidate> candidates =
            MakeRegistryCandidates();
        static_cast<void>(model_.Synchronize(candidates, active));
        const std::vector<TabStripItem> items = MakeRegistryStripItems();
        DWORD strip_error = ERROR_SUCCESS;
        if (!tab_strip_.SetItems(items, *active, &strip_error)) {
            fatal_error_ =
                L"Refreshing the in-memory tab name failed with Win32 error " +
                std::to_wstring(strip_error) + L'.';
            return;
        }
        WriteLine(
            logger_,
            std::wstring(updated == InMemoryTabNameUpdateResult::Stored
                             ? L"TAB_NAME_MEMORY_SET "
                             : L"TAB_NAME_MEMORY_CLEARED ") +
                FormatIdentity(identity) + L" length=" +
                std::to_wstring(name.size()));
    }

private:
    void ApplyProfileNameUpdate(
        const WindowIdentity& identity,
        const std::wstring_view name) {
        if (!profile_name_persistence_available_ ||
            profile_tab_names_ == nullptr || profile_resolver_ == nullptr) {
            return;
        }
        const auto selected = std::find_if(
            registry_.windows().begin(), registry_.windows().end(),
            [&identity](const ChromeWindowSnapshot& snapshot) {
                return WindowIdentitiesMatch(identity, MakeIdentity(snapshot));
            });
        if (selected == registry_.windows().end()) {
            return;
        }
        const ChromeProfileResolution selected_profile =
            profile_resolver_->Resolve(*selected);
        if (!selected_profile.matched()) {
            WriteLine(
                logger_,
                L"TAB_NAME_PROFILE_FALLBACK status=" +
                    std::wstring(ChromeProfileResolutionStatusName(
                        selected_profile.status)) +
                    L"; memory name remains active.",
                true);
            return;
        }

        std::size_t shared_window_count = 0;
        for (const ChromeWindowSnapshot& snapshot : registry_.windows()) {
            const ChromeProfileResolution candidate =
                profile_resolver_->Resolve(snapshot);
            if (!candidate.matched() ||
                candidate.profile_key != selected_profile.profile_key) {
                continue;
            }
            const InMemoryTabNameUpdateResult shared_update =
                in_memory_tab_names_->Set(MakeIdentity(snapshot), name);
            if (shared_update == InMemoryTabNameUpdateResult::Stored ||
                shared_update == InMemoryTabNameUpdateResult::Cleared) {
                ++shared_window_count;
            }
        }

        const InMemoryTabNameUpdateResult profile_update =
            profile_tab_names_->Set(selected_profile.profile_key, name);
        if (profile_update != InMemoryTabNameUpdateResult::Stored &&
            profile_update != InMemoryTabNameUpdateResult::Cleared) {
            WriteLine(
                logger_,
                L"TAB_NAME_PROFILE_REJECTED; memory name remains active.",
                true);
            return;
        }
        std::wstring save_error;
        if (!SaveProfileTabNamesAtomically(
                profile_tab_names_path_, profile_tab_names_->entries(),
                &save_error)) {
            WriteLine(
                logger_,
                L"TAB_NAME_PROFILE_SAVE_FAILED; memory name remains active: " +
                    save_error,
                true);
            return;
        }
        WriteLine(
            logger_,
            std::wstring(profile_update == InMemoryTabNameUpdateResult::Stored
                             ? L"TAB_NAME_PROFILE_SAVED"
                             : L"TAB_NAME_PROFILE_CLEARED") +
                L" shared_windows=" +
                std::to_wstring(shared_window_count) + L" length=" +
                std::to_wstring(name.size()));
    }

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

    void ApplyConfiguredTabStrip(
        WindowGroupGeometry* const geometry) const noexcept {
        if (geometry == nullptr || !geometry->valid) {
            return;
        }
        const int height = geometry->tab_strip_bounds.bottom -
                           geometry->tab_strip_bounds.top;
        const RECT configured = CalculateV31TabStripBounds(
            geometry->content_bounds,
            height,
            TabStripSurfaceMode::AttachedAbove,
            config_.tab_strip_alignment,
            config_.tab_strip_width_percent);
        if (configured.right > configured.left &&
            configured.bottom > configured.top) {
            geometry->tab_strip_bounds = configured;
        }
    }

    void ApplyMaximizedTabStrip(
        WindowGroupGeometry* const geometry,
        const HWND chrome_window) const noexcept {
        if (geometry == nullptr || !geometry->valid ||
            chrome_window == nullptr) {
            return;
        }
        const int height = geometry->tab_strip_bounds.bottom -
                           geometry->tab_strip_bounds.top;
        const RECT configured = CalculateV31TabStripBounds(
            geometry->content_bounds,
            height,
            TabStripSurfaceMode::MaximizedOverlay,
            config_.tab_strip_alignment,
            config_.tab_strip_width_percent,
            CaptionControlReserveForWindow(chrome_window));
        if (configured.right > configured.left &&
            configured.bottom > configured.top) {
            geometry->tab_strip_bounds = configured;
        }
    }

    [[nodiscard]] bool ApplyGroupGeometry(
        WindowGroupGeometry geometry,
        const WindowIdentity& active_identity,
        const std::wstring_view reason,
        const bool require_native_normal = false) {
        ApplyConfiguredTabStrip(&geometry);
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
        tab_strip_.SetSurfaceMode(TabStripSurfaceMode::AttachedAbove);
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

    [[nodiscard]] bool ApplyNativeMaximizedGeometry(
        WindowGroupGeometry geometry,
        const WindowIdentity& active_identity,
        const std::wstring_view reason) {
        ApplyMaximizedTabStrip(&geometry, active_identity.hwnd);
        if (!geometry.valid || !geometry_before_maximize_.valid) {
            fatal_error_ =
                L"V3.1 calculated an invalid native-maximized group geometry.";
            return false;
        }
        const WindowCoordinationResult arranged =
            placement_controller_.ArrangeAsMaximized(
                geometry_before_maximize_, active_identity);
        if (!arranged.succeeded) {
            fatal_error_ =
                L"V3.1 native maximize failed: " + arranged.message;
            return false;
        }
        tab_strip_.SetSurfaceMode(TabStripSurfaceMode::MaximizedOverlay);
        DWORD bounds_error = ERROR_SUCCESS;
        if (!tab_strip_.SetBounds(
                geometry.tab_strip_bounds, &bounds_error)) {
            fatal_error_ =
                L"Moving the V3.1 maximized overlay failed with Win32 error " +
                std::to_wstring(bounds_error) + L'.';
            return false;
        }
        if (model_.CanActivate(active_identity)) {
            static_cast<void>(model_.SetActive(active_identity));
            if (!UpdateActiveVisual(active_identity)) {
                fatal_error_ =
                    L"The V3.1 maximized overlay could not follow the active member.";
                return false;
            }
        }
        geometry_ = geometry;
        WriteLine(
            logger_,
            L"GROUP_GEOMETRY reason=" + std::wstring(reason) +
                L" mode=native-maximized left=" +
                std::to_wstring(geometry.group_bounds.left) + L" top=" +
                std::to_wstring(geometry.group_bounds.top) + L" right=" +
                std::to_wstring(geometry.group_bounds.right) + L" bottom=" +
                std::to_wstring(geometry.group_bounds.bottom) +
                L" overlay_right=" +
                std::to_wstring(geometry.tab_strip_bounds.right));
        return true;
    }

    [[nodiscard]] bool RefreshNativeMaximizedOverlay(
        const MonitorGeometry& monitor,
        const WindowIdentity& active_identity,
        const std::wstring_view reason) {
        WindowGroupGeometry proposed =
            CalculateNativeMaximizedGroupGeometry(
                monitor.work_area,
                TabStripHeightForWindow(active_identity.hwnd));
        ApplyMaximizedTabStrip(&proposed, active_identity.hwnd);
        if (!proposed.valid) {
            fatal_error_ =
                L"V3.1 calculated an invalid maximized overlay refresh.";
            return false;
        }
        tab_strip_.SetSurfaceMode(TabStripSurfaceMode::MaximizedOverlay);
        if (geometry_.valid &&
            RectanglesEqual(geometry_.group_bounds, proposed.group_bounds) &&
            RectanglesEqual(
                geometry_.tab_strip_bounds, proposed.tab_strip_bounds)) {
            return true;
        }
        DWORD bounds_error = ERROR_SUCCESS;
        if (!tab_strip_.SetBounds(
                proposed.tab_strip_bounds, &bounds_error)) {
            fatal_error_ =
                L"Refreshing the V3.1 maximized overlay failed with Win32 error " +
                std::to_wstring(bounds_error) + L'.';
            return false;
        }
        geometry_ = proposed;
        WriteLine(
            logger_,
            L"GROUP_GEOMETRY reason=" + std::wstring(reason) +
                L" mode=native-maximized-overlay-refresh");
        return true;
    }

    [[nodiscard]] std::optional<WindowIdentity>
    FindNativeStateDriver(const bool maximized) const {
        const HWND foreground = GetForegroundWindow();
        std::optional<WindowIdentity> first;
        for (const ChromeWindowSnapshot& snapshot : registry_.windows()) {
            if (IsIconic(snapshot.hwnd) != FALSE ||
                (IsZoomed(snapshot.hwnd) != FALSE) != maximized) {
                continue;
            }
            const WindowIdentity identity = MakeIdentity(snapshot);
            if (snapshot.hwnd == foreground) {
                return identity;
            }
            if (!first.has_value()) {
                first = identity;
            }
        }
        return first;
    }

    [[nodiscard]] bool SynchronizeGroupGeometry() {
        std::optional<WindowIdentity> driver = GeometryDriver();
        if (view_state_ == GroupViewState::Maximized) {
            if (const auto restored = FindNativeStateDriver(false);
                restored.has_value()) {
                driver = restored;
            }
        } else if (view_state_ == GroupViewState::Normal) {
            if (const auto maximized = FindNativeStateDriver(true);
                maximized.has_value()) {
                driver = maximized;
            }
        }
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
            const bool restored = restore_maximized
                ? ApplyNativeMaximizedGeometry(
                      CalculateNativeMaximizedGroupGeometry(
                          monitor.work_area,
                          TabStripHeightForWindow(driver->hwnd)),
                      *driver,
                      L"restore-native-maximized")
                : ApplyGroupGeometry(
                      geometry_before_minimize_,
                      *driver,
                      L"restore-normal",
                      true);
            if (!restored ||
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
                if (!ApplyNativeMaximizedGeometry(
                        CalculateNativeMaximizedGroupGeometry(
                            monitor.work_area,
                            TabStripHeightForWindow(driver->hwnd)),
                        *driver,
                        L"fullscreen-exit-native-maximized") ||
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

        if (view_state_ == GroupViewState::Maximized) {
            if (IsZoomed(driver->hwnd) != FALSE) {
                return RefreshNativeMaximizedOverlay(
                    monitor, *driver, L"native-maximized-stable");
            }
            view_state_ = GroupViewState::Normal;
            if (!ApplyGroupGeometry(
                    geometry_before_maximize_,
                    *driver,
                    L"native-maximize-restore",
                    true) ||
                !SetTabStripVisibility(true)) {
                return false;
            }
            WriteLine(logger_, L"GROUP_STATE NORMAL");
            return true;
        }

        if (IsZoomed(driver->hwnd) != FALSE) {
            geometry_before_maximize_ = geometry_;
            view_state_ = GroupViewState::Maximized;
            if (!ApplyNativeMaximizedGeometry(
                    CalculateNativeMaximizedGroupGeometry(
                        monitor.work_area,
                        TabStripHeightForWindow(driver->hwnd)),
                    *driver,
                    L"native-maximize") ||
                !SetTabStripVisibility(true)) {
                return false;
            }
            WriteLine(logger_, L"GROUP_STATE MAXIMIZED");
            return true;
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
        if (!WindowGroupArrangementRequired(
                geometry_, moved, driver_bounds)) {
            return true;
        }
        if (RectanglesEqual(moved.group_bounds, geometry_.group_bounds) &&
            !RectanglesEqual(moved.content_bounds, driver_bounds)) {
            WriteLine(
                logger_,
                L"GROUP_GEOMETRY_CORRECTION driver=" +
                    FormatIdentity(*driver) +
                    L" actual=[" + std::to_wstring(driver_bounds.left) +
                    L"," + std::to_wstring(driver_bounds.top) + L"," +
                    std::to_wstring(driver_bounds.right) + L"," +
                    std::to_wstring(driver_bounds.bottom) +
                    L"] target=[" +
                    std::to_wstring(moved.content_bounds.left) + L"," +
                    std::to_wstring(moved.content_bounds.top) + L"," +
                    std::to_wstring(moved.content_bounds.right) + L"," +
                    std::to_wstring(moved.content_bounds.bottom) + L"]");
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
            case ChromeWindowEventKind::MoveSizeEnded:
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

    [[nodiscard]] std::vector<std::wstring> ResolveDisplayNames(
        const std::span<const ChromeWindowSnapshot> windows) const {
        std::vector<std::wstring> names =
            ResolveTabDisplayNames(tab_name_rules_, windows);
        for (std::size_t index = 0; index < windows.size(); ++index) {
            if (profile_name_persistence_available_ &&
                profile_tab_names_ != nullptr &&
                profile_resolver_ != nullptr) {
                const ChromeProfileResolution profile =
                    profile_resolver_->Resolve(windows[index]);
                if (profile.matched()) {
                    names[index] = profile_tab_names_->Resolve(
                        profile.profile_key, names[index]);
                }
            }
            if (in_memory_tab_names_ != nullptr) {
                names[index] = in_memory_tab_names_->Resolve(
                    MakeIdentity(windows[index]), names[index]);
            }
        }
        return names;
    }

    [[nodiscard]] std::vector<TabGroupCandidate>
    MakeRegistryCandidates() const {
        std::vector<TabGroupCandidate> candidates;
        candidates.reserve(registry_.windows().size());
        const std::vector<std::wstring> display_names =
            ResolveDisplayNames(registry_.windows());
        for (std::size_t index = 0;
             index < registry_.windows().size();
             ++index) {
            const ChromeWindowSnapshot& snapshot =
                registry_.windows()[index];
            candidates.push_back({
                .identity = MakeIdentity(snapshot),
                .title = display_names[index],
            });
        }
        return candidates;
    }

    [[nodiscard]] std::vector<TabStripItem> MakeRegistryStripItems() const {
        std::vector<TabStripItem> items;
        items.reserve(registry_.windows().size());
        const std::vector<std::wstring> display_names =
            ResolveDisplayNames(registry_.windows());
        for (std::size_t index = 0;
             index < registry_.windows().size();
             ++index) {
            const ChromeWindowSnapshot& snapshot =
                registry_.windows()[index];
            items.push_back({
                .identity = MakeIdentity(snapshot),
                .title = display_names[index],
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
        ApplyConfiguredTabStrip(&geometry_);
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
        if (scan.windows.size() > kMaximumManagedChromeWindows) {
            ManagedChromeWindowSelection selection =
                SelectManagedChromeWindows(
                    scan.windows,
                    registry_.windows(),
                    GetForegroundWindow(),
                    kMaximumManagedChromeWindows);
            const std::size_t previous_ignored = ignored_window_count_;
            ignored_window_count_ = selection.overflow.size();
            if (previous_ignored != ignored_window_count_) {
                WriteLine(
                    logger_,
                    L"WINDOW_LIMIT managed=" +
                        std::to_wstring(selection.selected.size()) +
                        L" ignored=" +
                        std::to_wstring(selection.overflow.size()) +
                        L"; existing managed identities stay grouped and overflow windows keep their own taskbar entries.");
            }
            scan.windows = std::move(selection.selected);
        } else if (ignored_window_count_ != 0) {
            ignored_window_count_ = 0;
            WriteLine(
                logger_,
                L"WINDOW_LIMIT_CLEARED all manageable Chrome windows are within the supported range.");
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
            geometry_before_minimize_ = {};
            geometry_before_maximize_ = {};
            geometry_before_fullscreen_ = {};
            view_state_ = GroupViewState::Normal;
            state_before_minimize_ = GroupViewState::Normal;
            state_before_fullscreen_ = GroupViewState::Normal;
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
                view_state_ == GroupViewState::Maximized
                    ? placement_controller_.ArrangeAsMaximized(
                          geometry_before_maximize_, desired_active)
                    : placement_controller_.Arrange(
                          geometry_, desired_active);
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
    AppConfig config_;
    std::vector<TabNameRule> tab_name_rules_;
    InMemoryTabNameStore* in_memory_tab_names_ = nullptr;
    ProfileTabNameStore* profile_tab_names_ = nullptr;
    ChromeProfileResolver* profile_resolver_ = nullptr;
    std::filesystem::path profile_tab_names_path_;
    bool profile_name_persistence_available_ = false;
    std::size_t ignored_window_count_ = 0;
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
    std::wstring last_runtime_error_;
};

constexpr UINT kV2TrayCallbackMessage = WM_APP + 20;
constexpr UINT_PTR kV2TrayTimerId = 20;
constexpr UINT kV2TrayIconId = 1;
constexpr int kV2ApplicationIconResourceId = 101;
constexpr UINT kV2MenuStatus = 200;
constexpr UINT kV2MenuScanNow = 201;
constexpr UINT kV2MenuPause = 202;
constexpr UINT kV2MenuResume = 203;
constexpr UINT kV2MenuRestoreAll = 204;
constexpr UINT kV2MenuStartWithWindows = 205;
constexpr UINT kV2MenuOpenLogs = 206;
constexpr UINT kV2MenuAbout = 207;
constexpr UINT kV2MenuExit = 208;
constexpr UINT kV2MenuProviderBuiltIn = 209;
constexpr UINT kV2MenuProviderWindowTabs = 210;
constexpr UINT kV2MenuPersistProfileTabNames = 211;
constexpr int kV2HotKeyPreviousTab = 301;
constexpr int kV2HotKeyNextTab = 302;
constexpr UINT kV2ManagedTimerMilliseconds = 16;
constexpr UINT kV2IdleTimerMilliseconds = 500;
constexpr wchar_t kV2ProjectUrl[] =
    L"https://github.com/yangyunzhao/ChromeTaskbarMerger";

class BuiltInTrayApplication final {
public:
    BuiltInTrayApplication(
        const HINSTANCE instance,
        Logger* const logger,
        const AppConfig& config,
        std::filesystem::path v1_recovery_path,
        std::filesystem::path group_recovery_path,
        std::filesystem::path configuration_path,
        std::filesystem::path executable_path)
        : instance_(instance),
          logger_(logger),
          config_(config),
          configured_provider_(config.tab_provider),
          configured_profile_name_persistence_(
              config.persist_tab_names_by_profile),
          v1_recovery_path_(std::move(v1_recovery_path)),
          group_recovery_path_(std::move(group_recovery_path)),
          configuration_path_(std::move(configuration_path)),
          executable_path_(std::move(executable_path)),
          state_(TabProvider::BuiltIn) {}

    BuiltInTrayApplication(const BuiltInTrayApplication&) = delete;
    BuiltInTrayApplication& operator=(const BuiltInTrayApplication&) = delete;

    [[nodiscard]] int Run() {
        if (!CreateHiddenWindow() || !AddTrayIcon()) {
            CleanupShell();
            return 4;
        }
        SynchronizeAutoStartRegistration();
        InitializeManagement();
        UpdateTimer();

        MSG message{};
        int exit_code = 0;
        while (true) {
            const BOOL received = GetMessageW(&message, nullptr, 0, 0);
            if (received == 0) {
                break;
            }
            if (received == -1) {
                LogError(
                    L"The built-in tray message loop failed with Win32 error " +
                    std::to_wstring(GetLastError()) + L'.');
                exit_code = 6;
                break;
            }
            if (session_ != nullptr &&
                session_->ProcessThreadMessage(message)) {
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (session_ != nullptr &&
            !session_->PauseManaged(L"Built-in tray message-loop exit")) {
            exit_code = 5;
        }
        session_.reset();
        CleanupShell();
        return exit_code;
    }

private:
    [[nodiscard]] bool CreateHiddenWindow() {
        large_icon_ = static_cast<HICON>(LoadImageW(
            instance_,
            MAKEINTRESOURCEW(kV2ApplicationIconResourceId),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXICON),
            GetSystemMetrics(SM_CYICON),
            LR_SHARED));
        small_icon_ = static_cast<HICON>(LoadImageW(
            instance_,
            MAKEINTRESOURCEW(kV2ApplicationIconResourceId),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_SHARED));

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &BuiltInTrayApplication::WindowProcedure;
        window_class.hInstance = instance_;
        window_class.hIcon = large_icon_ != nullptr
                                 ? large_icon_
                                 : LoadIconW(nullptr, IDI_APPLICATION);
        window_class.hIconSm = small_icon_ != nullptr
                                   ? small_icon_
                                   : LoadIconW(nullptr, IDI_APPLICATION);
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = kTrayWindowClassName;
        if (RegisterClassExW(&window_class) == 0) {
            LogError(
                L"Registering the built-in tray window failed with Win32 error " +
                std::to_wstring(GetLastError()) + L'.');
            return false;
        }
        class_registered_ = true;
        taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
        if (taskbar_created_message_ == 0) {
            LogError(
                L"RegisterWindowMessageW(TaskbarCreated) failed with Win32 error " +
                std::to_wstring(GetLastError()) + L'.');
            return false;
        }
        window_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kTrayWindowClassName,
            L"ChromeTaskbarMerger",
            WS_OVERLAPPED,
            0,
            0,
            0,
            0,
            nullptr,
            nullptr,
            instance_,
            this);
        if (window_ == nullptr) {
            LogError(
                L"Creating the built-in tray window failed with Win32 error " +
                std::to_wstring(GetLastError()) + L'.');
            return false;
        }
        return true;
    }

    [[nodiscard]] bool AddTrayIcon() {
        if (window_ == nullptr) {
            return false;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = kV2TrayIconId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = kV2TrayCallbackMessage;
        data.hIcon = small_icon_ != nullptr
                         ? small_icon_
                         : LoadIconW(nullptr, IDI_APPLICATION);
        const std::wstring tooltip = BuildTooltip();
        wcsncpy_s(data.szTip, tooltip.c_str(), _TRUNCATE);
        if (Shell_NotifyIconW(NIM_ADD, &data) == FALSE) {
            LogError(L"Adding the built-in tray icon failed.");
            return false;
        }
        data.uVersion = NOTIFYICON_VERSION_4;
        static_cast<void>(Shell_NotifyIconW(NIM_SETVERSION, &data));
        tray_icon_added_ = true;
        return true;
    }

    void RemoveTrayIcon() noexcept {
        if (!tray_icon_added_ || window_ == nullptr) {
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = kV2TrayIconId;
        static_cast<void>(Shell_NotifyIconW(NIM_DELETE, &data));
        tray_icon_added_ = false;
    }

    void CleanupShell() noexcept {
        SetHotKeysEnabled(false);
        if (window_ != nullptr) {
            KillTimer(window_, kV2TrayTimerId);
        }
        RemoveTrayIcon();
        if (window_ != nullptr && IsWindow(window_) != FALSE) {
            DestroyWindow(window_);
        }
        window_ = nullptr;
        if (class_registered_) {
            UnregisterClassW(kTrayWindowClassName, instance_);
            class_registered_ = false;
        }
    }

    void InitializeManagement() {
        std::wstring legacy_path_error;
        const std::filesystem::path legacy_path =
            GetTabNameRulesPath(&legacy_path_error);
        if (legacy_path.empty()) {
            LogError(
                L"Legacy custom tab-name rules are unavailable: " +
                legacy_path_error);
        } else {
            TabNameLoadResult legacy_names = LoadTabNameRules(legacy_path);
            if (!legacy_names.succeeded) {
                LogError(
                    L"The legacy tab-name rule file was ignored because it is invalid: " +
                    legacy_names.error_message);
            } else {
                tab_name_rules_ = std::move(legacy_names.rules);
                LogInfo(
                    L"Loaded " + std::to_wstring(tab_name_rules_.size()) +
                    L" legacy custom tab-name rule(s).");
            }
        }
        if (!config_.persist_tab_names_by_profile) {
            LogInfo(
                L"Profile-linked tab-name persistence is disabled; Phase 6 memory names remain available.");
        } else {
            std::wstring path_error;
            profile_tab_names_path_ = GetProfileTabNamesPath(&path_error);
            if (profile_tab_names_path_.empty()) {
                LogError(
                    L"Profile-linked tab-name persistence is unavailable: " +
                    path_error);
            } else {
                ProfileTabNameLoadResult names =
                    LoadProfileTabNames(profile_tab_names_path_);
                if (!names.succeeded ||
                    !profile_tab_names_.Replace(std::move(names.entries))) {
                    LogError(
                        L"The profile tab-name file was ignored; memory fallback remains active: " +
                        (names.error_message.empty()
                             ? L"the loaded entries failed validation."
                             : names.error_message));
                    ShowNotification(
                        L"持久化标签名称未加载",
                        L"名称文件无效，本次运行已安全回退为内存名称。",
                        NIIF_WARNING);
                } else {
                    profile_name_persistence_available_ = true;
                    LogInfo(
                        L"Profile-linked tab-name persistence is ready; loaded " +
                        std::to_wstring(profile_tab_names_.size()) +
                        L" hashed name(s).");
                }
            }
        }
        const ProcessPresenceResult windowtabs =
            QueryWindowTabsPresence();
        const bool conflict = !windowtabs.query_succeeded ||
                              windowtabs.running;
        state_.CompleteInitialization(true, conflict);
        if (conflict) {
            LogError(
                windowtabs.running
                    ? L"Built-in tabs are paused because WindowTabs is running."
                    : L"Built-in tabs are paused because WindowTabs presence could not be verified.");
            ShowNotification(
                L"标签管理器冲突",
                windowtabs.running
                    ? L"当前选择内置标签。请退出 WindowTabs 后自动重试。"
                    : L"无法确认 WindowTabs 状态，内置管理已安全暂停。",
                NIIF_WARNING);
            return;
        }
        static_cast<void>(TryPrepareGroup());
    }

    [[nodiscard]] bool RecoveryJournalIsClear() const {
        GroupRecoveryJournal journal(group_recovery_path_);
        const GroupRecoveryLoadResult load = journal.Load();
        return load.succeeded && !load.state.HasObligations();
    }

    [[nodiscard]] bool TryPrepareGroup() {
        if (session_ != nullptr ||
            (!state_.preparing() && !state_.managing())) {
            return session_ != nullptr;
        }

        const ProcessPresenceResult windowtabs =
            QueryWindowTabsPresence();
        if (!windowtabs.query_succeeded || windowtabs.running) {
            state_.ConflictDetected(true);
            UpdateStatus();
            return false;
        }

        ManageableScan scan = ScanManageableChromeWindows(logger_);
        if (!scan.succeeded) {
            if (state_.preparing()) {
                state_.PreparationCompleted(false, true);
            } else {
                state_.OperationFailed(true);
            }
            LogError(L"Built-in tray Chrome enumeration failed.");
            UpdateStatus();
            return false;
        }
        last_window_count_ = scan.windows.size();
        if (scan.windows.empty()) {
            const bool was_preparing = state_.preparing();
            if (was_preparing) {
                state_.PreparationCompleted(true);
                LogInfo(L"Built-in provider is ready; no Chrome windows are open.");
                UpdateStatus();
            }
            return true;
        }
        if (scan.windows.size() > kMaximumManagedChromeWindows) {
            if (state_.preparing()) {
                state_.PreparationCompleted(false, true);
            } else {
                state_.OperationFailed(true);
            }
            LogError(L"Built-in Phase 6 currently supports at most five Chrome windows.");
            ShowNotification(
                L"窗口数量超出当前范围",
                L"Phase 6 最多管理 5 个 Chrome 窗口；请关闭多余窗口后恢复管理。",
                NIIF_WARNING);
            UpdateStatus();
            return false;
        }
        const auto non_normal = std::find_if(
            scan.windows.begin(),
            scan.windows.end(),
            [](const ChromeWindowSnapshot& snapshot) {
                return WindowShowState(snapshot.hwnd) != L"normal";
            });
        if (non_normal != scan.windows.end()) {
            if (state_.preparing()) {
                state_.PreparationCompleted(false, true);
            } else {
                state_.OperationFailed(true);
            }
            LogError(
                L"Built-in preparation requires every initial Chrome window to be normal.");
            ShowNotification(
                L"Chrome 窗口状态不适合建组",
                L"请先还原所有最小化或最大化窗口，再选择“恢复管理”。",
                NIIF_WARNING);
            UpdateStatus();
            return false;
        }

        auto candidate = std::make_unique<V2ExperimentSession>(
            instance_,
            logger_,
            v1_recovery_path_,
            group_recovery_path_,
            std::move(scan.windows),
            config_,
            tab_name_rules_,
            &in_memory_tab_names_,
            &profile_tab_names_,
            &profile_resolver_,
            profile_tab_names_path_,
            profile_name_persistence_available_);
        if (!candidate->Prepare()) {
            candidate.reset();
            const bool restored = RecoveryJournalIsClear();
            if (state_.preparing()) {
                state_.PreparationCompleted(false, restored);
            } else {
                state_.OperationFailed(restored);
            }
            LogError(L"Built-in tray group preparation failed.");
            ShowNotification(
                restored ? L"内置标签准备失败" : L"恢复未完成",
                restored
                    ? L"窗口已恢复，可从托盘重试。"
                    : L"请使用“恢复全部”并查看日志。",
                restored ? NIIF_WARNING : NIIF_ERROR);
            UpdateStatus();
            return false;
        }
        session_ = std::move(candidate);
        last_window_count_ = session_->window_count();
        last_ignored_window_count_ = session_->ignored_window_count();
        if (state_.preparing()) {
            state_.PreparationCompleted(true);
        }
        LogInfo(
            L"Built-in Phase 5 tray management started with " +
            std::to_wstring(last_window_count_) + L" Chrome window(s).");
        ShowNotification(
            L"内置标签管理已启动",
            L"Chrome 已建立内置标签组，任务栏仅保留一个入口。",
            NIIF_INFO);
        UpdateStatus();
        return true;
    }

    void HandleTimer() {
        const ULONGLONG now = GetTickCount64();
        if (now >= next_environment_check_) {
            next_environment_check_ = now + kV2IdleTimerMilliseconds;
            const ProcessPresenceResult windowtabs =
                QueryWindowTabsPresence();
            const bool conflict = !windowtabs.query_succeeded ||
                                  windowtabs.running;
            if (session_ != nullptr && conflict) {
                const bool restored = session_->PauseManaged(
                    L"WindowTabs conflict restoration");
                if (restored) {
                    session_.reset();
                }
                state_.ConflictDetected(restored);
                ShowNotification(
                    restored ? L"标签管理器冲突" : L"恢复未完成",
                    restored
                        ? L"检测到 WindowTabs，内置标签已解除并暂停。"
                        : L"检测到 WindowTabs，但窗口恢复未完成。",
                    restored ? NIIF_WARNING : NIIF_ERROR);
                UpdateStatus();
                return;
            }
            if (session_ == nullptr && state_.managing() && conflict) {
                state_.ConflictDetected(true);
                ShowNotification(
                    L"标签管理器冲突",
                    L"检测到 WindowTabs，内置模式已暂停等待冲突解除。",
                    NIIF_WARNING);
                UpdateStatus();
                return;
            }
            if (session_ == nullptr && state_.paused_by_conflict() &&
                !conflict && state_.ConflictCleared()) {
                static_cast<void>(TryPrepareGroup());
            } else if (session_ == nullptr && state_.managing() &&
                       !conflict && now >= next_group_scan_) {
                next_group_scan_ = now +
                    static_cast<ULONGLONG>(config_.scan_interval.count());
                static_cast<void>(TryPrepareGroup());
            }
        }

        if (session_ == nullptr) {
            return;
        }
        const std::size_t previous_count = last_window_count_;
        const std::size_t previous_ignored = last_ignored_window_count_;
        const V2ManagedPumpResult pumped = session_->PumpManagedWork();
        last_window_count_ = session_->window_count();
        last_ignored_window_count_ = session_->ignored_window_count();
        if (pumped == V2ManagedPumpResult::Active &&
            previous_ignored != last_ignored_window_count_) {
            ShowNotification(
                last_ignored_window_count_ == 0
                    ? L"窗口数量已回到支持范围"
                    : L"额外 Chrome 窗口保持独立",
                last_ignored_window_count_ == 0
                    ? L"未管理窗口已加入内置组。"
                    : L"内置组继续管理原有 5 个窗口；多余窗口保留自己的任务栏入口。",
                last_ignored_window_count_ == 0 ? NIIF_INFO : NIIF_WARNING);
        }
        if (pumped == V2ManagedPumpResult::SafelyStopped) {
            const std::wstring error = session_->last_runtime_error();
            session_.reset();
            state_.OperationFailed(true);
            LogError(L"Built-in management paused after recovery: " + error);
            ShowNotification(
                L"内置管理异常暂停",
                L"任务栏和窗口布局已恢复，可从托盘重试。",
                NIIF_WARNING);
        } else if (pumped == V2ManagedPumpResult::RecoveryRequired) {
            state_.RequireRecovery();
            ShowNotification(
                L"需要恢复",
                L"内置标签会话未能完全恢复，请使用“恢复全部”。",
                NIIF_ERROR);
        }
        if (pumped != V2ManagedPumpResult::Active ||
            previous_count != last_window_count_ ||
            previous_ignored != last_ignored_window_count_) {
            UpdateStatus();
        }
    }

    void PauseManagement() {
        if (!state_.can_pause()) {
            return;
        }
        bool restored = true;
        if (session_ != nullptr) {
            restored = session_->PauseManaged(L"Tray user pause");
            if (restored) {
                session_.reset();
            }
        }
        state_.PauseByUser(restored);
        ShowNotification(
            restored ? L"管理已暂停" : L"暂停恢复未完成",
            restored
                ? L"任务栏和每个 Chrome 的原始位置已恢复。"
                : L"请使用“恢复全部”并查看日志。",
            restored ? NIIF_INFO : NIIF_ERROR);
        UpdateStatus();
    }

    void ResumeManagement() {
        if (state_.recovery_required()) {
            ShowNotification(
                L"需要先恢复",
                L"请先使用“恢复全部”，再恢复管理。",
                NIIF_WARNING);
            return;
        }
        if (!state_.can_resume()) {
            return;
        }
        const ProcessPresenceResult windowtabs =
            QueryWindowTabsPresence();
        const bool conflict = !windowtabs.query_succeeded ||
                              windowtabs.running;
        if (!state_.ResumeRequested(true, conflict)) {
            return;
        }
        if (conflict) {
            ShowNotification(
                L"标签管理器冲突",
                L"请退出 WindowTabs；内置模式随后会自动准备。",
                NIIF_WARNING);
            UpdateStatus();
            return;
        }
        static_cast<void>(TryPrepareGroup());
    }

    [[nodiscard]] bool ForceRestoreAll() {
        if (session_ != nullptr) {
            bool restored = session_->PauseManaged(
                L"Tray explicit restoration");
            if (!restored) {
                restored = session_->RetryManagedRecovery(
                    L"Tray explicit restoration retry");
            }
            if (!restored) {
                state_.RequireRecovery();
                UpdateStatus();
                return false;
            }
            session_.reset();
        }
        const bool succeeded = RunStandaloneRestoreAll(
                                   logger_,
                                   v1_recovery_path_,
                                   group_recovery_path_) == 0;
        state_.ExplicitRestoreCompleted(succeeded);
        ShowNotification(
            succeeded ? L"恢复完成" : L"恢复未完成",
            succeeded
                ? L"任务栏和窗口布局已恢复，管理保持暂停。"
                : L"请保留恢复日志并重试。",
            succeeded ? NIIF_INFO : NIIF_ERROR);
        UpdateStatus();
        return succeeded;
    }

    void ScanNow() {
        if (session_ != nullptr) {
            static_cast<void>(session_->RequestImmediateSynchronization());
            return;
        }
        if (state_.managing() || state_.preparing()) {
            static_cast<void>(TryPrepareGroup());
            return;
        }
        const ManageableScan scan = ScanManageableChromeWindows(logger_);
        if (scan.succeeded) {
            last_window_count_ = scan.windows.size();
            ShowNotification(
                L"只读扫描完成",
                L"管理仍保持暂停。",
                NIIF_INFO);
            UpdateStatus();
        }
    }

    void SelectProvider(const TabProvider provider) {
        if (configured_provider_ == provider) {
            return;
        }
        if (state_.recovery_required()) {
            ShowNotification(
                L"需要先恢复",
                L"请先使用“恢复全部”，再切换标签方式。",
                NIIF_WARNING);
            return;
        }
        if (session_ != nullptr || state_.can_pause()) {
            PauseManagement();
            if (state_.recovery_required()) {
                return;
            }
        }
        const AppConfigSaveResult saved =
            SaveTabProviderSetting(configuration_path_, provider);
        if (!saved.succeeded) {
            LogError(L"Saving tab_provider failed: " + saved.error_message);
            ShowNotification(
                L"标签方式未保存",
                L"无法原子更新配置，当前选择保持不变。",
                NIIF_ERROR);
            return;
        }
        configured_provider_ = provider;
        LogInfo(
            L"Tab provider saved as " +
            std::wstring(TabProviderDisplayName(provider)) +
            L"; restart is required.");
        ShowNotification(
            L"标签方式已保存",
            L"完全退出并重新启动后切换为“" +
                std::wstring(TabProviderDisplayName(provider)) + L"”。",
            NIIF_INFO);
        UpdateStatus();
    }

    void ToggleProfileTabNamePersistence() {
        const bool requested = !configured_profile_name_persistence_;
        const AppConfigSaveResult saved =
            SaveProfileTabNamePersistenceSetting(
                configuration_path_, requested);
        if (!saved.succeeded) {
            LogError(
                L"Saving persist_tab_names_by_profile failed: " +
                saved.error_message);
            ShowNotification(
                L"标签名称持久化设置未保存",
                L"无法原子更新配置，当前选择保持不变。",
                NIIF_ERROR);
            return;
        }
        configured_profile_name_persistence_ = requested;
        LogInfo(
            std::wstring(
                requested
                    ? L"Profile-linked tab-name persistence was enabled in configuration."
                    : L"Profile-linked tab-name persistence was disabled in configuration.") +
            L" Restart is required.");
        ShowNotification(
            requested ? L"已启用标签名称持久化"
                      : L"已关闭标签名称持久化",
            requested
                ? L"完全退出并重新启动后，内置标签名称将按本地 Chrome profile 保存。"
                : L"完全退出并重新启动后，只保留本次运行的内存名称。",
            NIIF_INFO);
    }

    void SynchronizeAutoStartRegistration() {
        const AutoStartOperationResult result =
            auto_start_registry_.SetEnabled(
                config_.start_with_windows, executable_path_);
        if (!result.succeeded) {
            LogError(
                L"Synchronizing Windows-login startup failed with Win32 error " +
                std::to_wstring(result.error_code) + L'.');
        }
    }

    void ToggleStartWithWindows() {
        const bool previous = config_.start_with_windows;
        const bool requested = !previous;
        const AppConfigSaveResult saved =
            SaveStartWithWindowsSetting(configuration_path_, requested);
        if (!saved.succeeded) {
            ShowNotification(
                L"自动启动设置未保存",
                L"无法写入配置，设置保持不变。",
                NIIF_ERROR);
            return;
        }
        const AutoStartOperationResult registry =
            auto_start_registry_.SetEnabled(requested, executable_path_);
        if (!registry.succeeded) {
            const AppConfigSaveResult rollback =
                SaveStartWithWindowsSetting(configuration_path_, previous);
            LogError(
                L"Updating login startup failed with Win32 error " +
                std::to_wstring(registry.error_code) +
                (rollback.succeeded
                     ? L"; configuration rolled back."
                     : L"; configuration rollback failed."));
            ShowNotification(
                L"自动启动设置未应用",
                L"Windows 启动项更新失败，已尝试恢复配置。",
                NIIF_ERROR);
            return;
        }
        config_.start_with_windows = requested;
        ShowNotification(
            requested ? L"已启用自动启动" : L"已关闭自动启动",
            requested ? L"下次登录 Windows 时将自动运行。"
                      : L"下次登录 Windows 时不会自动运行。",
            NIIF_INFO);
    }

    [[nodiscard]] std::wstring_view StateDisplayName() const noexcept {
        switch (state_.state()) {
            case ManagementState::Initializing:
                return L"正在初始化";
            case ManagementState::PreparingGroup:
                return L"正在准备内置标签";
            case ManagementState::WaitingForTabProvider:
                return L"等待标签提供器";
            case ManagementState::Managing:
                return session_ == nullptr ? L"管理中（等待 Chrome）"
                                           : L"管理中";
            case ManagementState::PausedByUser:
                return L"已暂停（用户）";
            case ManagementState::PausedByConflict:
                return L"已暂停（WindowTabs 冲突）";
            case ManagementState::PausedByError:
                return L"已暂停（异常）";
            case ManagementState::RecoveryRequired:
                return L"需要恢复";
        }
        return L"未知状态";
    }

    [[nodiscard]] std::wstring BuildTooltip() const {
        std::wstring tooltip =
            L"ChromeTaskbarMerger - " + std::wstring(StateDisplayName()) +
            L" - 内置标签 (Chrome " +
            std::to_wstring(last_window_count_);
        if (last_ignored_window_count_ != 0) {
            tooltip += L" + 独立 " +
                       std::to_wstring(last_ignored_window_count_);
        }
        return tooltip + L")";
    }

    void UpdateStatus() {
        if (session_ == nullptr) {
            last_ignored_window_count_ = 0;
        }
        SetHotKeysEnabled(session_ != nullptr && state_.managing());
        UpdateTimer();
        if (!tray_icon_added_ || window_ == nullptr) {
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = kV2TrayIconId;
        data.uFlags = NIF_TIP | NIF_SHOWTIP;
        const std::wstring tooltip = BuildTooltip();
        wcsncpy_s(data.szTip, tooltip.c_str(), _TRUNCATE);
        static_cast<void>(Shell_NotifyIconW(NIM_MODIFY, &data));
    }

    void SetHotKeysEnabled(const bool enabled) noexcept {
        if (window_ == nullptr) {
            hotkeys_registered_ = false;
            return;
        }
        if (!enabled) {
            if (hotkeys_registered_) {
                UnregisterHotKey(window_, kV2HotKeyPreviousTab);
                UnregisterHotKey(window_, kV2HotKeyNextTab);
                hotkeys_registered_ = false;
            }
            return;
        }
        if (hotkeys_registered_) {
            return;
        }
        const UINT modifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
        const bool previous = RegisterHotKey(
                                  window_,
                                  kV2HotKeyPreviousTab,
                                  modifiers,
                                  VK_PRIOR) != FALSE;
        const DWORD previous_error = previous ? ERROR_SUCCESS : GetLastError();
        const bool next = RegisterHotKey(
                              window_,
                              kV2HotKeyNextTab,
                              modifiers,
                              VK_NEXT) != FALSE;
        const DWORD next_error = next ? ERROR_SUCCESS : GetLastError();
        if (previous && next) {
            hotkeys_registered_ = true;
            LogInfo(
                L"Built-in tab hotkeys registered: Ctrl+Alt+PageUp/PageDown.");
            return;
        }
        if (previous) {
            UnregisterHotKey(window_, kV2HotKeyPreviousTab);
        }
        if (next) {
            UnregisterHotKey(window_, kV2HotKeyNextTab);
        }
        LogError(
            L"Built-in tab hotkeys are unavailable; previous_error=" +
            std::to_wstring(previous_error) + L" next_error=" +
            std::to_wstring(next_error) + L'.');
    }

    void UpdateTimer() {
        if (window_ == nullptr) {
            return;
        }
        KillTimer(window_, kV2TrayTimerId);
        const UINT interval = session_ != nullptr
                                  ? kV2ManagedTimerMilliseconds
                                  : kV2IdleTimerMilliseconds;
        if (SetTimer(window_, kV2TrayTimerId, interval, nullptr) == 0) {
            LogError(
                L"Creating the built-in tray timer failed with Win32 error " +
                std::to_wstring(GetLastError()) + L'.');
            if (session_ != nullptr) {
                const bool restored = session_->PauseManaged(
                    L"Tray timer failure restoration");
                if (restored) {
                    session_.reset();
                }
                state_.OperationFailed(restored);
            }
        }
    }

    void ShowNotification(const std::wstring_view title,
                          const std::wstring_view text,
                          const DWORD flags) const {
        if (!tray_icon_added_ || window_ == nullptr) {
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = kV2TrayIconId;
        data.uFlags = NIF_INFO;
        wcsncpy_s(data.szInfoTitle, std::wstring(title).c_str(), _TRUNCATE);
        wcsncpy_s(data.szInfo, std::wstring(text).c_str(), _TRUNCATE);
        data.dwInfoFlags = flags;
        static_cast<void>(Shell_NotifyIconW(NIM_MODIFY, &data));
    }

    void ShowContextMenu(POINT point) {
        HMENU menu = CreatePopupMenu();
        HMENU provider_menu = CreatePopupMenu();
        if (menu == nullptr || provider_menu == nullptr) {
            if (provider_menu != nullptr) {
                DestroyMenu(provider_menu);
            }
            if (menu != nullptr) {
                DestroyMenu(menu);
            }
            return;
        }
        const std::wstring status =
            L"状态：" + std::wstring(StateDisplayName());
        AppendMenuW(
            menu, MF_STRING | MF_DISABLED, kV2MenuStatus, status.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kV2MenuScanNow, L"立即重新扫描");
        AppendMenuW(
            menu,
            MF_STRING | MF_DISABLED,
            0,
            L"快捷切换：Ctrl+Alt+PageUp / PageDown");
        AppendMenuW(
            menu,
            MF_STRING | MF_DISABLED,
            0,
            L"双击内置标签可临时修改名称");
        AppendMenuW(
            menu,
            MF_STRING | (state_.can_pause() ? MF_ENABLED : MF_GRAYED),
            kV2MenuPause,
            L"暂停管理");
        AppendMenuW(
            menu,
            MF_STRING | (state_.can_resume() ? MF_ENABLED : MF_GRAYED),
            kV2MenuResume,
            L"恢复管理");
        AppendMenuW(
            menu, MF_STRING, kV2MenuRestoreAll, L"恢复全部任务栏和窗口布局");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(
            provider_menu,
            MF_STRING |
                (configured_provider_ == TabProvider::BuiltIn
                     ? MF_CHECKED
                     : MF_UNCHECKED),
            kV2MenuProviderBuiltIn,
            L"内置标签（默认）");
        AppendMenuW(
            provider_menu,
            MF_STRING |
                (configured_provider_ == TabProvider::WindowTabs
                     ? MF_CHECKED
                     : MF_UNCHECKED),
            kV2MenuProviderWindowTabs,
            L"WindowTabs 标签");
        AppendMenuW(
            menu,
            MF_POPUP,
            reinterpret_cast<UINT_PTR>(provider_menu),
            L"标签提供方式（重启生效）");
        AppendMenuW(
            menu,
            MF_STRING |
                (configured_profile_name_persistence_ ? MF_CHECKED
                                                      : MF_UNCHECKED),
            kV2MenuPersistProfileTabNames,
            L"按 Chrome profile 保存标签名称（重启生效）");
        AppendMenuW(
            menu,
            MF_STRING |
                (config_.start_with_windows ? MF_CHECKED : MF_UNCHECKED),
            kV2MenuStartWithWindows,
            L"随 Windows 登录自动启动");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kV2MenuOpenLogs, L"打开日志目录");
        AppendMenuW(menu, MF_STRING, kV2MenuAbout, L"关于 ChromeTaskbarMerger");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kV2MenuExit, L"退出");

        SetForegroundWindow(window_);
        const UINT command = TrackPopupMenu(
            menu,
            TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
            point.x,
            point.y,
            0,
            window_,
            nullptr);
        DestroyMenu(menu);
        PostMessageW(window_, WM_NULL, 0, 0);
        HandleMenuCommand(command);
    }

    void HandleMenuCommand(const UINT command) {
        switch (command) {
            case kV2MenuScanNow:
                ScanNow();
                break;
            case kV2MenuPause:
                PauseManagement();
                break;
            case kV2MenuResume:
                ResumeManagement();
                break;
            case kV2MenuRestoreAll:
                static_cast<void>(ForceRestoreAll());
                break;
            case kV2MenuProviderBuiltIn:
                SelectProvider(TabProvider::BuiltIn);
                break;
            case kV2MenuProviderWindowTabs:
                SelectProvider(TabProvider::WindowTabs);
                break;
            case kV2MenuPersistProfileTabNames:
                ToggleProfileTabNamePersistence();
                break;
            case kV2MenuStartWithWindows:
                ToggleStartWithWindows();
                break;
            case kV2MenuOpenLogs:
                OpenLogDirectory();
                break;
            case kV2MenuAbout:
                ShowAboutDialog();
                break;
            case kV2MenuExit:
                RequestExit();
                break;
            default:
                break;
        }
    }

    void OpenLogDirectory() const {
        if (logger_ == nullptr || logger_->log_directory().empty()) {
            return;
        }
        const std::filesystem::path directory = logger_->log_directory();
        static_cast<void>(ShellExecuteW(
            window_, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    }

    void ShowAboutDialog() const {
        const std::wstring content =
            L"ChromeTaskbarMerger " + std::wstring(kVersion) +
            L"\n\n内置窗口标签与 Windows 任务栏 Chrome 单入口工具"
            L"\n\n开发人员：杨云召"
            L"\n\nGitHub：" + std::wstring(kV2ProjectUrl) +
            L"\n\n许可证：MIT";
        MessageBoxW(
            window_,
            content.c_str(),
            L"关于 ChromeTaskbarMerger",
            MB_OK | MB_ICONINFORMATION);
    }

    void RequestExit() {
        if (session_ != nullptr) {
            if (!session_->PauseManaged(L"Built-in tray normal exit")) {
                state_.RequireRecovery();
                ShowNotification(
                    L"暂时无法退出",
                    L"窗口恢复未完成，请先使用“恢复全部”。",
                    NIIF_ERROR);
                UpdateStatus();
                return;
            }
            session_.reset();
        }
        RemoveTrayIcon();
        DestroyWindow(window_);
    }

    void LogInfo(const std::wstring_view message) const {
        if (logger_ != nullptr) {
            logger_->Info(message);
        }
    }

    void LogError(const std::wstring_view message) const {
        if (logger_ != nullptr) {
            logger_->Error(message);
        }
    }

    LRESULT HandleMessage(const UINT message,
                          const WPARAM wparam,
                          const LPARAM lparam) {
        if (taskbar_created_message_ != 0 &&
            message == taskbar_created_message_) {
            tray_icon_added_ = false;
            const bool tray_rebuilt = AddTrayIcon();
            if (session_ != nullptr) {
                static_cast<void>(session_->HandleTaskbarRecreated());
            }
            LogInfo(
                tray_rebuilt
                    ? L"TaskbarCreated rebuilt the built-in tray icon and scheduled managed Shell state reconstruction."
                    : L"TaskbarCreated could not rebuild the built-in tray icon; managed Shell reconstruction was still attempted.");
            return 0;
        }
        switch (message) {
            case WM_TIMER:
                if (wparam == kV2TrayTimerId) {
                    HandleTimer();
                }
                return 0;
            case WM_HOTKEY:
                if (session_ != nullptr &&
                    (wparam == kV2HotKeyPreviousTab ||
                     wparam == kV2HotKeyNextTab)) {
                    static_cast<void>(session_->ActivateRelativeTab(
                        wparam == kV2HotKeyNextTab ? 1 : -1));
                }
                return 0;
            case WM_DISPLAYCHANGE:
                if (session_ != nullptr) {
                    static_cast<void>(
                        session_->RequestDisplayEnvironmentSynchronization(
                            L"WM_DISPLAYCHANGE"));
                }
                return 0;
            case WM_SETTINGCHANGE:
                if (session_ != nullptr &&
                    wparam == SPI_SETWORKAREA) {
                    static_cast<void>(
                        session_->RequestDisplayEnvironmentSynchronization(
                            L"SPI_SETWORKAREA"));
                }
                return 0;
            case WM_POWERBROADCAST:
                if (session_ != nullptr &&
                    (wparam == PBT_APMRESUMEAUTOMATIC ||
                     wparam == PBT_APMRESUMESUSPEND)) {
                    static_cast<void>(
                        session_->RequestDisplayEnvironmentSynchronization(
                            L"POWER_RESUME"));
                }
                return TRUE;
            case kExternalCommandMessage:
                if (wparam == static_cast<WPARAM>(
                                  ExistingInstanceCommand::RestoreAll)) {
                    return ForceRestoreAll() ? 1 : 0;
                }
                if (wparam == static_cast<WPARAM>(
                                  ExistingInstanceCommand::Rescan)) {
                    ScanNow();
                    return 1;
                }
                return 0;
            case kV2TrayCallbackMessage: {
                const UINT notification = LOWORD(lparam);
                if (notification == WM_CONTEXTMENU ||
                    notification == WM_RBUTTONUP ||
                    notification == NIN_SELECT ||
                    notification == NIN_KEYSELECT) {
                    POINT point{GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam)};
                    if (notification != WM_CONTEXTMENU ||
                        (point.x == -1 && point.y == -1)) {
                        GetCursorPos(&point);
                    }
                    ShowContextMenu(point);
                }
                return 0;
            }
            case WM_CLOSE:
                RequestExit();
                return 0;
            case WM_QUERYENDSESSION:
                if (session_ == nullptr) {
                    return TRUE;
                }
                if (!session_->PauseManaged(
                        L"Windows session-end restoration")) {
                    state_.RequireRecovery();
                    return FALSE;
                }
                session_.reset();
                state_.PauseByUser(true);
                return TRUE;
            case WM_DESTROY:
                window_ = nullptr;
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(window_, message, wparam, lparam);
        }
    }

    static LRESULT CALLBACK WindowProcedure(
        const HWND window,
        const UINT message,
        const WPARAM wparam,
        const LPARAM lparam) {
        BuiltInTrayApplication* application = nullptr;
        if (message == WM_NCCREATE) {
            const auto* const create =
                reinterpret_cast<const CREATESTRUCTW*>(lparam);
            application = static_cast<BuiltInTrayApplication*>(
                create->lpCreateParams);
            application->window_ = window;
            SetWindowLongPtrW(
                window,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(application));
        } else {
            application = reinterpret_cast<BuiltInTrayApplication*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
        }
        return application != nullptr
                   ? application->HandleMessage(message, wparam, lparam)
                   : DefWindowProcW(window, message, wparam, lparam);
    }

    HINSTANCE instance_ = nullptr;
    Logger* logger_ = nullptr;
    AppConfig config_;
    TabProvider configured_provider_ = TabProvider::BuiltIn;
    bool configured_profile_name_persistence_ = false;
    std::filesystem::path v1_recovery_path_;
    std::filesystem::path group_recovery_path_;
    std::filesystem::path configuration_path_;
    std::filesystem::path executable_path_;
    HWND window_ = nullptr;
    UINT taskbar_created_message_ = 0;
    HICON large_icon_ = nullptr;
    HICON small_icon_ = nullptr;
    bool class_registered_ = false;
    bool tray_icon_added_ = false;
    bool hotkeys_registered_ = false;
    std::size_t last_window_count_ = 0;
    std::size_t last_ignored_window_count_ = 0;
    ULONGLONG next_environment_check_ = 0;
    ULONGLONG next_group_scan_ = 0;
    AutoStartRegistry auto_start_registry_;
    ManagementStateMachine state_;
    std::vector<TabNameRule> tab_name_rules_;
    InMemoryTabNameStore in_memory_tab_names_;
    ProfileTabNameStore profile_tab_names_;
    ChromeProfileResolver profile_resolver_;
    std::filesystem::path profile_tab_names_path_;
    bool profile_name_persistence_available_ = false;
    std::unique_ptr<V2ExperimentSession> session_;
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

int RunBuiltInTrayApplication(
    const HINSTANCE instance,
    Logger* const logger,
    const AppConfig& config,
    const std::filesystem::path& recovery_journal_path,
    const std::filesystem::path& group_recovery_journal_path,
    const std::filesystem::path& configuration_path,
    const std::filesystem::path& executable_path) {
    try {
        BuiltInTrayApplication application(
            instance,
            logger,
            config,
            recovery_journal_path,
            group_recovery_journal_path,
            configuration_path,
            executable_path);
        return application.Run();
    } catch (const std::exception& exception) {
        if (logger != nullptr) {
            logger->Error(
                std::string("Unhandled built-in tray exception: ") +
                exception.what());
        }
        return 6;
    } catch (...) {
        if (logger != nullptr) {
            logger->Error(L"Unhandled unknown built-in tray exception.");
        }
        return 6;
    }
}

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
