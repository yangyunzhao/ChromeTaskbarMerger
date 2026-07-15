#include "auto_start.h"

#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void Expect(const bool condition, const std::string_view description) {
    if (condition) {
        return;
    }
    std::cerr << "FAILED: " << description << '\n';
    ++failures;
}

class TemporaryRegistrySubkey final {
public:
    TemporaryRegistrySubkey()
        : path_(L"Software\\ChromeTaskbarMerger-AutoStart-Test-" +
                std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64())) {
        RegDeleteTreeW(HKEY_CURRENT_USER, path_.c_str());
    }

    ~TemporaryRegistrySubkey() {
        RegDeleteTreeW(HKEY_CURRENT_USER, path_.c_str());
    }

    TemporaryRegistrySubkey(const TemporaryRegistrySubkey&) = delete;
    TemporaryRegistrySubkey& operator=(const TemporaryRegistrySubkey&) =
        delete;

    [[nodiscard]] const std::wstring& path() const noexcept {
        return path_;
    }

private:
    std::wstring path_;
};

void TestCommandQuotesPortableUnicodePath() {
    const std::filesystem::path executable =
        L"C:\\便携 工具\\ChromeTaskbarMerger.exe";
    const std::wstring command = ctm::BuildAutoStartCommand(executable);
    Expect(
        command ==
            L"\"C:\\便携 工具\\ChromeTaskbarMerger.exe\" --autostart",
        "the startup command should quote a portable path and add its marker");
}

void TestRegistryRegistrationLifecycle() {
    TemporaryRegistrySubkey temporary;
    ctm::AutoStartRegistry registry(
        HKEY_CURRENT_USER, temporary.path(), L"ChromeTaskbarMerger");
    const std::filesystem::path first_executable =
        L"C:\\Portable Apps\\ChromeTaskbarMerger.exe";
    const std::filesystem::path moved_executable =
        L"D:\\Tools\\ChromeTaskbarMerger.exe";

    ctm::AutoStartRegistrationStatus status =
        registry.Query(first_executable);
    Expect(status.succeeded && !status.registered,
           "a fresh test key should report no registration");

    ctm::AutoStartOperationResult operation =
        registry.SetEnabled(true, first_executable);
    Expect(operation.succeeded && operation.changed,
           "enabling should create the per-user registration");
    status = registry.Query(first_executable);
    Expect(status.succeeded && status.registered &&
               status.matches_expected_command,
           "the created registration should exactly match the executable");

    operation = registry.SetEnabled(true, first_executable);
    Expect(operation.succeeded && !operation.changed,
           "enabling an exact registration should be idempotent");

    operation = registry.SetEnabled(true, moved_executable);
    Expect(operation.succeeded && operation.changed,
           "enabling from a moved portable directory should repair the path");
    status = registry.Query(moved_executable);
    Expect(status.succeeded && status.matches_expected_command,
           "the repaired registration should point at the new path");

    operation = registry.SetEnabled(false, moved_executable);
    Expect(operation.succeeded && operation.changed,
           "disabling should delete the registration");
    status = registry.Query(moved_executable);
    Expect(status.succeeded && !status.registered,
           "the deleted registration should remain absent");

    operation = registry.SetEnabled(false, moved_executable);
    Expect(operation.succeeded && !operation.changed,
           "disabling an absent registration should be idempotent");
}

void TestRunCommandLengthLimitIsEnforced() {
    TemporaryRegistrySubkey temporary;
    ctm::AutoStartRegistry registry(
        HKEY_CURRENT_USER, temporary.path(), L"ChromeTaskbarMerger");
    const std::filesystem::path long_executable =
        L"C:\\" + std::wstring(270, L'a') + L"\\ChromeTaskbarMerger.exe";
    const ctm::AutoStartOperationResult result =
        registry.SetEnabled(true, long_executable);
    Expect(!result.succeeded &&
               result.error_code == ERROR_FILENAME_EXCED_RANGE,
           "a Run command longer than the Windows limit should be rejected");
}

}  // namespace

int main() {
    TestCommandQuotesPortableUnicodePath();
    TestRegistryRegistrationLifecycle();
    TestRunCommandLengthLimitIsEnforced();

    if (failures != 0) {
        std::cerr << failures << " auto-start test(s) failed.\n";
        return 1;
    }
    std::cout << "All auto-start tests passed.\n";
    return 0;
}
