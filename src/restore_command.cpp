#include "restore_command.h"

#include "chrome_window.h"
#include "fixed_entry_manager.h"
#include "group_recovery_journal.h"
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

[[nodiscard]] bool RestoreTrackedWithRetry(
    FixedEntryManager* const manager,
    Logger* const logger,
    const std::wstring_view label) {
    FixedEntryReport report = manager->RestoreAll();
    std::wstring message =
        std::wstring(label) + L": " +
        std::wstring(report.succeeded ? L"SUCCESS" : L"FAIL") +
        L"; tracked=" + std::to_wstring(manager->removed_window_count()) +
        L"; " + report.message;
    WriteMessage(logger, !report.succeeded, message);
    if (report.succeeded && manager->removed_window_count() == 0) {
        return true;
    }
    report = manager->RestoreAll();
    message = std::wstring(label) + L" retry: " +
              std::wstring(report.succeeded ? L"SUCCESS" : L"FAIL") +
              L"; tracked=" +
              std::to_wstring(manager->removed_window_count()) + L"; " +
              report.message;
    WriteMessage(logger, !report.succeeded, message);
    return report.succeeded && manager->removed_window_count() == 0;
}

void LogLayoutReport(Logger* const logger,
                     const std::wstring_view label,
                     const GroupLayoutRecoveryReport& report) {
    for (const GroupLayoutRecoveryOperation& operation :
         report.operations) {
        std::wostringstream message;
        message << label << L": "
                << (operation.succeeded ? L"SUCCESS" : L"FAIL")
                << L"; HWND=0x" << std::hex << std::uppercase
                << reinterpret_cast<std::uintptr_t>(
                       operation.identity.hwnd)
                << std::dec << L"; skipped="
                << (operation.safely_skipped ? L"yes" : L"no")
                << L"; Win32=" << operation.win32_error << L"; "
                << operation.message;
        WriteMessage(logger, !operation.succeeded, message.str());
    }
}

[[nodiscard]] bool RestoreLoadedGroup(
    GroupRecoveryJournal* const journal,
    TaskbarController* const controller,
    Logger* const logger,
    const std::wstring_view label) {
    FixedEntryManager manager(controller, journal);
    std::wstring adopt_error;
    if (!manager.AdoptRecoveryStates(
            journal->state().taskbar_states, &adopt_error)) {
        WriteMessage(
            logger,
            true,
            std::wstring(label) +
                L": adopting persisted taskbar state failed: " +
                adopt_error);
        return false;
    }
    if (!RestoreTrackedWithRetry(
            &manager,
            logger,
            std::wstring(label) + L" taskbar restoration")) {
        return false;
    }

    Win32GroupRecoveryWindowGateway gateway;
    const GroupLayoutRecoveryReport layout =
        RestorePersistedGroupLayouts(journal, &gateway);
    LogLayoutReport(logger, std::wstring(label) + L" layout restoration", layout);
    if (!layout.succeeded) {
        if (!layout.persistence_error.empty()) {
            WriteMessage(
                logger,
                true,
                std::wstring(label) +
                    L": persisting layout completion failed: " +
                    layout.persistence_error);
        }
        return false;
    }
    return true;
}

}  // namespace

StartupGroupRecoveryResult RestorePreviousGroupSession(
    Logger* const logger,
    const std::filesystem::path& group_recovery_journal_path) {
    StartupGroupRecoveryResult result;
    GroupRecoveryJournal journal(group_recovery_journal_path);
    GroupRecoveryLoadResult load = journal.Load();
    if (!load.succeeded) {
        result.error_message =
            L"The V2 group recovery journal is invalid or unreadable: " +
            load.error_message;
        WriteMessage(logger, true, result.error_message);
        return result;
    }
    std::wstring adopt_error;
    if (!journal.Adopt(std::move(load.state), &adopt_error)) {
        result.error_message =
            L"Adopting the V2 group recovery journal failed: " +
            adopt_error;
        WriteMessage(logger, true, result.error_message);
        return result;
    }
    if (!journal.state().HasObligations()) {
        result.succeeded = true;
        return result;
    }

    result.recovery_attempted = true;
    WriteMessage(
        logger,
        false,
        L"A previous V2 group session was not closed cleanly; taskbar-first recovery is starting.");
    TaskbarController controller;
    const TaskbarOperationResult initialization =
        controller.InitializeTaskbarList();
    if (!initialization.succeeded) {
        result.error_message =
            L"ITaskbarList initialization for startup group recovery failed: " +
            initialization.message;
        WriteMessage(logger, true, result.error_message);
        return result;
    }
    if (!RestoreLoadedGroup(
            &journal, &controller, logger, L"Startup V2 recovery")) {
        result.error_message =
            L"The previous V2 group could not be recovered completely.";
        return result;
    }
    std::wstring clear_error;
    if (!journal.Clear(&clear_error)) {
        result.error_message =
            L"Clearing the recovered V2 journal failed: " + clear_error;
        WriteMessage(logger, true, result.error_message);
        return result;
    }
    WriteMessage(
        logger,
        false,
        L"The previous V2 taskbar and window layout state was restored before startup.");
    result.succeeded = true;
    return result;
}

int RunStandaloneRestoreAll(
    Logger* const logger,
    const std::filesystem::path& recovery_journal_path,
    const std::filesystem::path& group_recovery_journal_path) {
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

    bool succeeded = RestoreTrackedWithRetry(
        &manager, logger, L"Explicit V1 persisted-state restoration");

    GroupRecoveryJournal group_journal(group_recovery_journal_path);
    GroupRecoveryLoadResult group_load = group_journal.Load();
    const bool group_journal_valid = group_load.succeeded;
    if (!group_journal_valid) {
        WriteMessage(
            logger,
            true,
            L"The V2 group recovery journal is invalid; taskbar entries will be forced visible, but no persisted layout will be moved and the invalid journal will be retained. " +
                group_load.error_message);
        succeeded = false;
    } else {
        std::wstring group_adopt_error;
        if (!group_journal.Adopt(
                std::move(group_load.state), &group_adopt_error)) {
            WriteMessage(
                logger,
                true,
                L"Adopting V2 group recovery state failed: " +
                    group_adopt_error);
            succeeded = false;
        } else if (!RestoreLoadedGroup(
                       &group_journal,
                       &controller,
                       logger,
                       L"Explicit V2 recovery")) {
            succeeded = false;
        }
    }
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

    if (group_journal_valid && succeeded) {
        std::wstring clear_error;
        if (!group_journal.Clear(&clear_error)) {
            WriteMessage(
                logger,
                true,
                L"Clearing the V2 group recovery journal failed: " +
                    clear_error);
            succeeded = false;
        }
    }

    if (succeeded) {
        WriteMessage(
            logger,
            false,
            L"All currently identifiable Chrome taskbar buttons and valid persisted V2 layouts were explicitly restored.");
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
