#pragma once

#include <Windows.h>

#include <cstddef>
#include <filesystem>
#include <string>

namespace ctm {

inline constexpr std::size_t kMaximumRunCommandLength = 260;

struct AutoStartRegistrationStatus {
    bool succeeded = false;
    bool registered = false;
    bool matches_expected_command = false;
    DWORD error_code = ERROR_SUCCESS;
    std::wstring registered_command;
};

struct AutoStartOperationResult {
    bool succeeded = false;
    bool changed = false;
    DWORD error_code = ERROR_SUCCESS;
};

[[nodiscard]] std::wstring BuildAutoStartCommand(
    const std::filesystem::path& executable_path);

class AutoStartRegistry final {
public:
    AutoStartRegistry();
    AutoStartRegistry(HKEY root,
                      std::wstring subkey,
                      std::wstring value_name);

    [[nodiscard]] AutoStartRegistrationStatus Query(
        const std::filesystem::path& executable_path) const;
    [[nodiscard]] AutoStartOperationResult SetEnabled(
        bool enabled,
        const std::filesystem::path& executable_path) const;

private:
    HKEY root_ = nullptr;
    std::wstring subkey_;
    std::wstring value_name_;
};

}  // namespace ctm
