#include "auto_start.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace ctm {
namespace {

constexpr wchar_t kRunSubkey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"ChromeTaskbarMerger";

class RegistryKey final {
public:
    RegistryKey() = default;
    ~RegistryKey() {
        if (value_ != nullptr) {
            RegCloseKey(value_);
        }
    }

    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;

    [[nodiscard]] HKEY* address() noexcept {
        return &value_;
    }

    [[nodiscard]] HKEY get() const noexcept {
        return value_;
    }

private:
    HKEY value_ = nullptr;
};

[[nodiscard]] AutoStartOperationResult Failure(const LSTATUS error_code) {
    return {
        .succeeded = false,
        .changed = false,
        .error_code = static_cast<DWORD>(error_code),
    };
}

}  // namespace

std::wstring BuildAutoStartCommand(
    const std::filesystem::path& executable_path) {
    if (executable_path.empty()) {
        return {};
    }
    return L'"' + executable_path.wstring() + L"\" --autostart";
}

AutoStartRegistry::AutoStartRegistry()
    : AutoStartRegistry(HKEY_CURRENT_USER, kRunSubkey, kRunValueName) {}

AutoStartRegistry::AutoStartRegistry(HKEY root,
                                     std::wstring subkey,
                                     std::wstring value_name)
    : root_(root),
      subkey_(std::move(subkey)),
      value_name_(std::move(value_name)) {}

AutoStartRegistrationStatus AutoStartRegistry::Query(
    const std::filesystem::path& executable_path) const {
    AutoStartRegistrationStatus status;
    if (root_ == nullptr || subkey_.empty() || value_name_.empty()) {
        status.error_code = ERROR_INVALID_PARAMETER;
        return status;
    }

    RegistryKey key;
    const LSTATUS open_status = RegOpenKeyExW(
        root_, subkey_.c_str(), 0, KEY_QUERY_VALUE, key.address());
    if (open_status == ERROR_FILE_NOT_FOUND) {
        status.succeeded = true;
        return status;
    }
    if (open_status != ERROR_SUCCESS) {
        status.error_code = static_cast<DWORD>(open_status);
        return status;
    }

    DWORD byte_count = 0;
    LSTATUS query_status = RegGetValueW(
        key.get(), nullptr, value_name_.c_str(), RRF_RT_REG_SZ, nullptr,
        nullptr, &byte_count);
    if (query_status == ERROR_FILE_NOT_FOUND) {
        status.succeeded = true;
        return status;
    }
    if (query_status != ERROR_SUCCESS) {
        status.error_code = static_cast<DWORD>(query_status);
        return status;
    }

    std::vector<wchar_t> buffer(
        std::max<std::size_t>(byte_count / sizeof(wchar_t) + 1U, 1U), L'\0');
    query_status = RegGetValueW(
        key.get(), nullptr, value_name_.c_str(), RRF_RT_REG_SZ, nullptr,
        buffer.data(), &byte_count);
    if (query_status != ERROR_SUCCESS) {
        status.error_code = static_cast<DWORD>(query_status);
        return status;
    }

    status.succeeded = true;
    status.registered = true;
    status.registered_command = buffer.data();
    status.matches_expected_command =
        status.registered_command == BuildAutoStartCommand(executable_path);
    return status;
}

AutoStartOperationResult AutoStartRegistry::SetEnabled(
    const bool enabled,
    const std::filesystem::path& executable_path) const {
    if (root_ == nullptr || subkey_.empty() || value_name_.empty()) {
        return Failure(ERROR_INVALID_PARAMETER);
    }

    if (!enabled) {
        RegistryKey key;
        const LSTATUS open_status = RegOpenKeyExW(
            root_, subkey_.c_str(), 0, KEY_SET_VALUE, key.address());
        if (open_status == ERROR_FILE_NOT_FOUND) {
            return {.succeeded = true};
        }
        if (open_status != ERROR_SUCCESS) {
            return Failure(open_status);
        }

        const LSTATUS delete_status =
            RegDeleteValueW(key.get(), value_name_.c_str());
        if (delete_status == ERROR_FILE_NOT_FOUND) {
            return {.succeeded = true};
        }
        if (delete_status != ERROR_SUCCESS) {
            return Failure(delete_status);
        }
        return {
            .succeeded = true,
            .changed = true,
        };
    }

    const std::wstring command = BuildAutoStartCommand(executable_path);
    if (command.empty()) {
        return Failure(ERROR_INVALID_PARAMETER);
    }
    if (command.size() > kMaximumRunCommandLength) {
        return Failure(ERROR_FILENAME_EXCED_RANGE);
    }

    const AutoStartRegistrationStatus current = Query(executable_path);
    if (current.succeeded && current.registered &&
        current.matches_expected_command) {
        return {.succeeded = true};
    }

    RegistryKey key;
    DWORD disposition = 0;
    const LSTATUS create_status = RegCreateKeyExW(
        root_, subkey_.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, key.address(), &disposition);
    if (create_status != ERROR_SUCCESS) {
        return Failure(create_status);
    }

    const DWORD byte_count =
        static_cast<DWORD>((command.size() + 1U) * sizeof(wchar_t));
    const LSTATUS set_status = RegSetValueExW(
        key.get(), value_name_.c_str(), 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()), byte_count);
    if (set_status != ERROR_SUCCESS) {
        return Failure(set_status);
    }

    const AutoStartRegistrationStatus verification = Query(executable_path);
    if (!verification.succeeded) {
        return Failure(verification.error_code);
    }
    if (!verification.registered ||
        !verification.matches_expected_command) {
        return Failure(ERROR_INVALID_DATA);
    }
    return {
        .succeeded = true,
        .changed = true,
    };
}

}  // namespace ctm
