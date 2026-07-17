#include "app_paths.h"

#include <Windows.h>

#include <vector>

namespace ctm {
namespace {

[[nodiscard]] std::wstring ReadEnvironmentVariable(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::vector<wchar_t> buffer(required);
    const DWORD written =
        GetEnvironmentVariableW(name, buffer.data(), required);
    if (written == 0 || written >= required) {
        return {};
    }
    return std::wstring(buffer.data(), written);
}

}  // namespace

std::filesystem::path GetExecutablePath(std::wstring* const error_message) {
    std::vector<wchar_t> buffer(1024);
    while (buffer.size() <= 32U * 1024U) {
        SetLastError(ERROR_SUCCESS);
        const DWORD copied = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            if (error_message != nullptr) {
                *error_message = L"GetModuleFileNameW failed with error " +
                                 std::to_wstring(GetLastError()) + L'.';
            }
            return {};
        }
        if (copied < buffer.size() - 1) {
            return std::filesystem::path(
                std::wstring(buffer.data(), copied));
        }
        buffer.resize(buffer.size() * 2);
    }
    if (error_message != nullptr) {
        *error_message = L"The executable path is unexpectedly long.";
    }
    return {};
}

std::filesystem::path GetConfigurationPath(
    std::wstring* const error_message) {
    const std::filesystem::path executable =
        GetExecutablePath(error_message);
    return executable.empty()
               ? std::filesystem::path{}
               : executable.parent_path() / L"ChromeTaskbarMerger.ini";
}

std::filesystem::path GetRecoveryJournalPath(
    std::wstring* const error_message) {
    const std::wstring local_app_data =
        ReadEnvironmentVariable(L"LOCALAPPDATA");
    if (local_app_data.empty()) {
        if (error_message != nullptr) {
            *error_message = L"LOCALAPPDATA is not available.";
        }
        return {};
    }
    return std::filesystem::path(local_app_data) /
           L"ChromeTaskbarMerger" / L"recovery-v1.tsv";
}

std::filesystem::path GetGroupRecoveryJournalPath(
    std::wstring* const error_message) {
    const std::wstring local_app_data =
        ReadEnvironmentVariable(L"LOCALAPPDATA");
    if (local_app_data.empty()) {
        if (error_message != nullptr) {
            *error_message = L"LOCALAPPDATA is not available.";
        }
        return {};
    }
    return std::filesystem::path(local_app_data) /
           L"ChromeTaskbarMerger" / L"recovery-v2.tsv";
}

}  // namespace ctm
