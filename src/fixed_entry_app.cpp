#include "fixed_entry_app.h"

#include "chrome_window.h"
#include "fixed_entry_manager.h"
#include "logger.h"
#include "taskbar_controller.h"

#include <Windows.h>
#include <TlHelp32.h>

#include <cstdint>
#include <cwchar>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ctm {
namespace {

class ConsoleCtrlCGuard final {
public:
    [[nodiscard]] bool Activate() {
        if (active_) {
            return true;
        }
        active_ = SetConsoleCtrlHandler(nullptr, TRUE) != FALSE;
        return active_;
    }

    ~ConsoleCtrlCGuard() {
        if (active_) {
            SetConsoleCtrlHandler(nullptr, FALSE);
        }
    }

    ConsoleCtrlCGuard() = default;
    ConsoleCtrlCGuard(const ConsoleCtrlCGuard&) = delete;
    ConsoleCtrlCGuard& operator=(const ConsoleCtrlCGuard&) = delete;

private:
    bool active_ = false;
};

class EmergencyRestoreGuard final {
public:
    EmergencyRestoreGuard(FixedEntryManager* const manager,
                          Logger* const logger) noexcept
        : manager_(manager), logger_(logger) {}

    ~EmergencyRestoreGuard() noexcept {
        if (manager_ == nullptr || manager_->removed_window_count() == 0) {
            return;
        }
        try {
            const FixedEntryReport report = manager_->RestoreAll();
            if (logger_ != nullptr) {
                if (report.succeeded &&
                    manager_->removed_window_count() == 0) {
                    logger_->Info(
                        L"Phase 3 scope-exit restoration completed.");
                } else {
                    logger_->Error(
                        L"Phase 3 scope-exit restoration did not complete.");
                }
            }
        } catch (...) {
            // A noexcept guard cannot recover from catastrophic failures.
        }
    }

    EmergencyRestoreGuard(const EmergencyRestoreGuard&) = delete;
    EmergencyRestoreGuard& operator=(const EmergencyRestoreGuard&) = delete;

private:
    FixedEntryManager* manager_ = nullptr;
    Logger* logger_ = nullptr;
};

struct ManageableWindowScan {
    bool succeeded = false;
    DWORD error_code = ERROR_SUCCESS;
    std::vector<ChromeWindowSnapshot> windows;
};

struct ProcessPresenceResult {
    bool query_succeeded = false;
    bool running = false;
    DWORD error_code = ERROR_SUCCESS;
};

[[nodiscard]] ProcessPresenceResult QueryWindowTabsPresence() {
    ProcessPresenceResult result;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        result.error_code = GetLastError();
        return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry) == FALSE) {
        result.error_code = GetLastError();
        CloseHandle(snapshot);
        return result;
    }

    do {
        if (_wcsicmp(entry.szExeFile, L"WindowTabs.exe") == 0) {
            result.running = true;
            break;
        }
    } while (Process32NextW(snapshot, &entry) != FALSE);

    CloseHandle(snapshot);
    result.query_succeeded = true;
    return result;
}

[[nodiscard]] bool RequireWindowTabs(Logger* const logger) {
    const ProcessPresenceResult presence = QueryWindowTabsPresence();
    if (presence.query_succeeded && presence.running) {
        if (logger != nullptr) {
            logger->Info(L"WindowTabs.exe reachability prerequisite is running.");
        }
        return true;
    }

    std::wstring message;
    if (!presence.query_succeeded) {
        message = L"WindowTabs process detection failed with Win32 error " +
                  std::to_wstring(presence.error_code) + L'.';
    } else {
        message =
            L"WindowTabs.exe is not running; no new taskbar entries will be removed.";
    }
    std::wcerr << message << L'\n';
    if (logger != nullptr) {
        logger->Error(message);
    }
    return false;
}

[[nodiscard]] std::wstring FormatHex(const std::uint64_t value,
                                     const int width) {
    std::wostringstream output;
    output << L"0x" << std::uppercase << std::hex << std::setfill(L'0')
           << std::setw(width) << value;
    return output.str();
}

[[nodiscard]] std::wstring FormatIdentity(const WindowIdentity& identity) {
    std::wostringstream output;
    output << L"HWND="
           << FormatHex(
                  static_cast<std::uint64_t>(
                      reinterpret_cast<std::uintptr_t>(identity.hwnd)),
                  static_cast<int>(sizeof(HWND) * 2))
           << L", PID/TID=" << identity.process_id << L'/'
           << identity.thread_id << L", Class=" << identity.class_name;
    return output.str();
}

[[nodiscard]] std::wstring FormatOperation(
    const FixedEntryOperation& operation) {
    const TaskbarOperationResult& result = operation.result;
    std::wostringstream output;
    output << (operation.kind == FixedEntryOperationKind::Remove
                   ? L"DeleteTab"
                   : L"AddTab")
           << L": " << (result.succeeded ? L"SUCCESS" : L"FAIL") << L'\n'
           << L"  " << FormatIdentity(operation.identity) << L'\n'
           << L"  HRESULT="
           << FormatHex(static_cast<std::uint32_t>(result.hresult), 8)
           << L", Win32=" << result.win32_error << L" ("
           << FormatHex(result.win32_error, 8) << L")\n"
           << L"  Changed=" << (result.state_changed ? L"yes" : L"no")
           << L", Skipped=" << (result.skipped ? L"yes" : L"no")
           << L"\n  Message=" << result.message;
    return output.str();
}

[[nodiscard]] std::wstring FormatReport(
    const std::wstring_view label,
    const FixedEntryReport& report,
    const std::size_t tracked_removals) {
    std::wostringstream output;
    output << label << L": " << (report.succeeded ? L"SUCCESS" : L"FAIL")
           << L'\n'
           << L"  Manageable windows: " << report.manageable_window_count
           << L'\n'
           << L"  Fixed main entry: ";
    if (report.main_entry.has_value()) {
        output << FormatIdentity(*report.main_entry);
    } else {
        output << L"none";
    }
    output << L'\n'
           << L"  Main entry changed: "
           << (report.main_entry_changed ? L"yes" : L"no") << L'\n'
           << L"  Already removed: " << report.already_removed_count << L'\n'
           << L"  Tracked removals after operation: " << tracked_removals
           << L'\n'
           << L"  Message: " << report.message;
    for (const FixedEntryOperation& operation : report.operations) {
        output << L"\n\n" << FormatOperation(operation);
    }
    return output.str();
}

void WriteReport(Logger* const logger, const std::wstring& report) {
    std::wcout << report << L'\n';
    if (logger != nullptr) {
        logger->Info(report);
    }
}

[[nodiscard]] ManageableWindowScan ScanManageableWindows(
    Logger* const logger,
    const bool print_windows) {
    ManageableWindowScan scan;
    const ChromeWindowEnumerationResult enumeration =
        EnumerateChromeWindows();
    if (!enumeration.succeeded) {
        scan.error_code = enumeration.error_code;
        const std::wstring message =
            L"Chrome window enumeration failed with Win32 error " +
            std::to_wstring(enumeration.error_code) + L'.';
        std::wcerr << message << L'\n';
        if (logger != nullptr) {
            logger->Error(message);
        }
        return scan;
    }

    std::size_t manageable_index = 0;
    for (const ChromeWindowRecord& record : enumeration.chrome_windows) {
        if (!record.assessment.manageable) {
            continue;
        }
        scan.windows.push_back(record.snapshot);
        ++manageable_index;
        if (print_windows) {
            const std::wstring formatted =
                FormatChromeWindowRecord(record, manageable_index);
            std::wcout << L"\n" << formatted << L'\n';
            if (logger != nullptr) {
                logger->Info(formatted);
            }
        }
    }

    scan.succeeded = true;
    const std::wstring summary =
        L"Phase 3 scan found " +
        std::to_wstring(scan.windows.size()) +
        L" manageable Chrome window(s).";
    std::wcout << summary << L'\n';
    if (logger != nullptr) {
        logger->Info(summary);
    }
    return scan;
}

[[nodiscard]] bool SynchronizeNow(FixedEntryManager* const manager,
                                  Logger* const logger,
                                  const HWND foreground_window) {
    const ManageableWindowScan scan =
        ScanManageableWindows(logger, true);
    if (!scan.succeeded) {
        return false;
    }

    const FixedEntryReport report =
        manager->Synchronize(scan.windows, foreground_window);
    WriteReport(
        logger,
        FormatReport(
            L"Fixed-entry synchronization",
            report,
            manager->removed_window_count()));
    return report.succeeded;
}

[[nodiscard]] bool RestoreWithRetry(FixedEntryManager* const manager,
                                    Logger* const logger,
                                    const std::wstring_view label) {
    FixedEntryReport report = manager->RestoreAll();
    WriteReport(
        logger,
        FormatReport(label, report, manager->removed_window_count()));
    if (report.succeeded && manager->removed_window_count() == 0) {
        return true;
    }

    std::wcerr << L"The first restoration attempt did not complete; "
                  L"retrying once.\n";
    report = manager->RestoreAll();
    WriteReport(
        logger,
        FormatReport(
            L"Restoration retry",
            report,
            manager->removed_window_count()));
    return report.succeeded && manager->removed_window_count() == 0;
}

void PrintCommands(const bool management_enabled) {
    std::wcout
        << L"\nCommands (management is "
        << (management_enabled ? L"enabled" : L"paused") << L"):\n"
        << L"  s  Scan now"
        << (management_enabled ? L" and synchronize\n"
                               : L" (read-only while paused)\n")
        << L"  a  Apply/resume management and scan now\n"
        << L"  r  Restore all entries changed by this session and pause\n"
        << L"  h  Show these commands\n"
        << L"  q  Restore all and exit\n";
}

[[nodiscard]] int RunFixedEntryApplicationImpl(Logger* const logger) {
    const HWND startup_foreground_window = GetForegroundWindow();
    std::wcout
        << L"=== ChromeTaskbarMerger Phase 3 fixed-entry MVP ===\n"
        << L"This session uses ITaskbarList::DeleteTab/AddTab only.\n"
        << L"Phase 2 showed that removed windows may be absent from Alt+Tab; "
           L"keep WindowTabs running so they remain reachable.\n"
        << L"This phase does not poll automatically. Use 's' after changing "
           L"the Chrome window set.\n"
        << L"After management starts, Ctrl+C is ignored; use 'q' so every "
           L"changed entry can be restored.\n\n";

    const ManageableWindowScan preview =
        ScanManageableWindows(logger, true);
    if (!preview.succeeded) {
        return 3;
    }

    std::wcout
        << L"\nThe startup foreground Chrome window will be kept when "
           L"possible; otherwise the lowest stable HWND is used.\n"
        << L"Type MANAGE to start the reversible session: " << std::flush;
    std::wstring confirmation;
    if (!std::getline(std::wcin, confirmation) || confirmation != L"MANAGE") {
        std::wcout << L"Management cancelled before any taskbar change.\n";
        return 0;
    }

    if (!RequireWindowTabs(logger)) {
        std::wcerr
            << L"Start WindowTabs, verify the Chrome windows are reachable, "
               L"and run --manage again.\n";
        return 4;
    }

    TaskbarController controller;
    const TaskbarOperationResult initialization =
        controller.InitializeTaskbarList();
    std::wostringstream initialization_report;
    initialization_report
        << L"Initialize ITaskbarList: "
        << (initialization.succeeded ? L"SUCCESS" : L"FAIL") << L'\n'
        << L"  HRESULT="
        << FormatHex(static_cast<std::uint32_t>(initialization.hresult), 8)
        << L", Win32=" << initialization.win32_error << L"\n"
        << L"  Message=" << initialization.message;
    WriteReport(logger, initialization_report.str());
    if (!initialization.succeeded) {
        return 4;
    }

    ConsoleCtrlCGuard ctrl_c_guard;
    if (!ctrl_c_guard.Activate()) {
        const DWORD error_code = GetLastError();
        std::wcerr << L"Unable to install the Ctrl+C safety guard (Win32 "
                      L"error "
                   << error_code << L"). No taskbar change was made.\n";
        return 4;
    }

    FixedEntryManager manager(&controller);
    EmergencyRestoreGuard emergency_restore(&manager, logger);
    bool management_enabled = true;
    bool operation_failed =
        !SynchronizeNow(&manager, logger, startup_foreground_window);
    PrintCommands(management_enabled);

    while (true) {
        std::wcout << L"\nphase3> " << std::flush;
        std::wstring command;
        if (!std::getline(std::wcin, command)) {
            std::wcout << L"Input ended; restoring and exiting.\n";
            break;
        }

        if (command == L"q" || command == L"Q") {
            break;
        }
        if (command == L"h" || command == L"H" || command == L"?") {
            PrintCommands(management_enabled);
            continue;
        }
        if (command == L"r" || command == L"R") {
            const bool restored = RestoreWithRetry(
                &manager, logger, L"Restore all and pause");
            management_enabled = false;
            operation_failed = operation_failed || !restored;
            PrintCommands(management_enabled);
            continue;
        }
        if (command == L"a" || command == L"A") {
            if (!RequireWindowTabs(logger)) {
                const bool restored = RestoreWithRetry(
                    &manager,
                    logger,
                    L"WindowTabs prerequisite restoration");
                management_enabled = false;
                operation_failed = true;
                static_cast<void>(restored);
            } else {
                management_enabled = true;
                const bool synchronized =
                    SynchronizeNow(&manager, logger, GetForegroundWindow());
                operation_failed = operation_failed || !synchronized;
            }
            PrintCommands(management_enabled);
            continue;
        }
        if (command == L"s" || command == L"S") {
            if (management_enabled) {
                if (!RequireWindowTabs(logger)) {
                    const bool restored = RestoreWithRetry(
                        &manager,
                        logger,
                        L"WindowTabs prerequisite restoration");
                    management_enabled = false;
                    operation_failed = true;
                    static_cast<void>(restored);
                    PrintCommands(management_enabled);
                } else {
                    const bool synchronized = SynchronizeNow(
                        &manager, logger, GetForegroundWindow());
                    operation_failed = operation_failed || !synchronized;
                }
            } else {
                const ManageableWindowScan scan =
                    ScanManageableWindows(logger, true);
                operation_failed = operation_failed || !scan.succeeded;
                std::wcout << L"Management remains paused; enter 'a' to "
                              L"apply the fixed-entry rule.\n";
            }
            continue;
        }

        std::wcout << L"Unknown command. Enter h for help.\n";
    }

    const bool restored =
        RestoreWithRetry(&manager, logger, L"Normal-exit restoration");
    if (!restored) {
        std::wcerr
            << L"WARNING: one or more taskbar entries could not be restored. "
               L"Do not start another management session; keep the log and "
               L"restart Explorer if an entry remains missing.\n";
        return 5;
    }

    std::wcout << L"All changes from this session were restored.\n";
    return operation_failed ? 4 : 0;
}

}  // namespace

int RunFixedEntryApplication(Logger* const logger) {
    try {
        return RunFixedEntryApplicationImpl(logger);
    } catch (const std::exception& exception) {
        if (logger != nullptr) {
            logger->Error(
                std::string("Unhandled Phase 3 exception: ") +
                exception.what());
        }
        std::wcerr
            << L"The Phase 3 session stopped after an unexpected exception; "
               L"scope-exit restoration was attempted.\n";
        return 6;
    } catch (...) {
        if (logger != nullptr) {
            logger->Error(L"Unhandled unknown Phase 3 exception.");
        }
        std::wcerr
            << L"The Phase 3 session stopped after an unknown exception; "
               L"scope-exit restoration was attempted.\n";
        return 6;
    }
}

}  // namespace ctm
