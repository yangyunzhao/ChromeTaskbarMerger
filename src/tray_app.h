#pragma once

#include "app_config.h"

#include <Windows.h>

#include <filesystem>

namespace ctm {

class Logger;

enum class ExistingInstanceCommand : WPARAM {
    Rescan = 1,
    RestoreAll = 2,
};

inline constexpr wchar_t kSingletonName[] =
    L"Local\\ChromeTaskbarMerger.Singleton";
inline constexpr wchar_t kTrayWindowClassName[] =
    L"ChromeTaskbarMerger.TrayWindow.v1";
inline constexpr UINT kExternalCommandMessage = WM_APP + 11;

[[nodiscard]] bool SendTrayInstanceCommand(
    HWND window,
    ExistingInstanceCommand command,
    bool wait_for_result,
    DWORD* error_code);

[[nodiscard]] bool NotifyExistingTrayInstance(
    ExistingInstanceCommand command,
    bool wait_for_result,
    DWORD* error_code);

[[nodiscard]] int RunTrayApplication(
    HINSTANCE instance,
    Logger* logger,
    const AppConfig& config,
    const std::filesystem::path& recovery_journal_path,
    const std::filesystem::path& group_recovery_journal_path,
    const std::filesystem::path& configuration_path,
    const std::filesystem::path& executable_path,
    bool launched_at_login);

}  // namespace ctm
