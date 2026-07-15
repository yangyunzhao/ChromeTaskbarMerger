#include "restore_command.h"

#include "chrome_window.h"
#include "fixed_entry_manager.h"
#include "logger.h"
#include "recovery_journal.h"
#include "taskbar_controller.h"

#include <Windows.h>

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace ctm {
namespace {

void WriteMessage(Logger* const logger,
                  const bool error,
                  const std::wstring& message) {
    (error ? std::wcerr : std::wcout) << message << L'\n';
    if (logger != nullptr) {
        if (error) {
            logger->Error(message);
        } else {
            logger->Info(message);
        }
    }
}

[[nodiscard]] bool RestoreTrackedWithRetry(FixedEntryManager* const manager,
                                           Logger* const logger) {
    FixedEntryReport report = manager->RestoreAll();
    std::wstring message =
        L"Explicit persisted-state restoration: " +
        std::wstring(report.succeeded ? L"SUCCESS" : L"FAIL") +
        L"; tracked=" + std::to_wstring(manager->removed_window_count()) +
        L"; " + report.message;
    WriteMessage(logger, !report.succeeded, message);
    if (report.succeeded && manager->removed_window_count() == 0) {
        return true;
    }
    report = manager->RestoreAll();
    message = L"Explicit persisted-state restoration retry: " +
              std::wstring(report.succeeded ? L"SUCCESS" : L"FAIL") +
              L"; tracked=" +
              std::to_wstring(manager->removed_window_count()) + L"; " +
              report.message;
    WriteMessage(logger, !report.succeeded, message);
    return report.succeeded && manager->removed_window_count() == 0;
}

}  // namespace

int RunStandaloneRestoreAll(
    Logger* const logger,
    const std::filesystem::path& recovery_journal_path) {
    RecoveryJournal journal(recovery_journal_path);
    const RecoveryLoadResult load = journal.Load();
    if (!load.succeeded) {
        WriteMessage(
            logger,
            false,
            L"WARNING: the persisted recovery journal is invalid; the "
            L"explicit command will restore currently manageable Chrome "
            L"windows only and replace the invalid journal on success. " +
                load.error_message);
    }

    TaskbarController controller;
    const TaskbarOperationResult initialization =
        controller.InitializeTaskbarList();
    if (!initialization.succeeded) {
        WriteMessage(
            logger,
            true,
            L"ITaskbarList initialization failed: " +
                initialization.message);
        return 4;
    }

    FixedEntryManager manager(&controller, &journal);
    if (load.succeeded) {
        std::wstring adopt_error;
        if (!manager.AdoptRecoveryStates(load.states, &adopt_error)) {
            WriteMessage(
                logger,
                true,
                L"Persisted recovery state is invalid: " + adopt_error);
            return 5;
        }
    }

    bool succeeded = RestoreTrackedWithRetry(&manager, logger);
    const ChromeWindowEnumerationResult enumeration =
        EnumerateChromeWindows();
    if (!enumeration.succeeded) {
        WriteMessage(
            logger,
            true,
            L"Chrome enumeration failed with Win32 error " +
                std::to_wstring(enumeration.error_code) + L'.');
        succeeded = false;
    } else {
        std::size_t restored_count = 0;
        for (const ChromeWindowRecord& record : enumeration.chrome_windows) {
            if (!record.assessment.manageable) {
                continue;
            }
            const TaskbarOperationResult result =
                controller.ForceRestoreWindow(record.snapshot);
            std::wostringstream message;
            message << L"Explicit AddTab: "
                    << (result.succeeded ? L"SUCCESS" : L"FAIL")
                    << L"; HWND=0x" << std::hex << std::uppercase
                    << reinterpret_cast<std::uintptr_t>(record.snapshot.hwnd)
                    << std::dec << L"; HRESULT=0x" << std::hex
                    << static_cast<std::uint32_t>(result.hresult)
                    << std::dec << L"; Win32=" << result.win32_error
                    << L"; " << result.message;
            WriteMessage(logger, !result.succeeded, message.str());
            succeeded = succeeded && result.succeeded;
            if (result.succeeded) {
                ++restored_count;
            }
        }
        WriteMessage(
            logger,
            false,
            L"Explicit restore processed " +
                std::to_wstring(restored_count) +
                L" manageable Chrome window(s).");
    }

    if (manager.removed_window_count() == 0) {
        std::wstring persistence_error;
        if (!manager.ResetAfterTaskbarRecreation(&persistence_error)) {
            WriteMessage(
                logger,
                true,
                L"Clearing the recovery journal failed: " +
                    persistence_error);
            succeeded = false;
        }
    } else {
        succeeded = false;
    }

    if (succeeded) {
        WriteMessage(
            logger,
            false,
            L"All currently identifiable Chrome taskbar buttons were "
            L"explicitly restored.");
        return 0;
    }
    WriteMessage(
        logger,
        true,
        L"One or more taskbar buttons could not be confirmed restored. "
        L"Keep the recovery journal and restart Explorer if needed.");
    return 5;
}

}  // namespace ctm
