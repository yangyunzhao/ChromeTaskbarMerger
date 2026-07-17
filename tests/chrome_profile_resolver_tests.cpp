#include "chrome_profile_resolver.h"
#include "chrome_window.h"

#include <Windows.h>
#include <Objbase.h>

#include <filesystem>
#include <iostream>
#include <cwctype>
#include <optional>
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

void TestInfoCacheParsingPreservesUnicodeAndSafetyFlags() {
    const std::string json =
        R"({"profile":{"info_cache":{"Default":{"name":"\u5de5\u4f5c","is_ephemeral":false},"Profile 1":{"name":"Personal \ud83d\ude80","is_ephemeral":true}}},"unrelated":[1,true,null]})";
    const ctm::ChromeProfileMetadataParseResult parsed =
        ctm::ParseChromeProfileInfoCacheJson(json);
    Expect(parsed.succeeded && parsed.entries.size() == 2,
           "a bounded Local State fixture should expose two profiles");
    if (parsed.entries.size() == 2) {
        Expect(parsed.entries[0].directory == "Default" &&
                   parsed.entries[0].display_name == L"工作" &&
                   !parsed.entries[0].is_ephemeral,
               "a regular Unicode profile should decode exactly");
        Expect(parsed.entries[1].is_ephemeral,
               "an ephemeral profile should retain its fail-closed flag");
    }
    const ctm::ChromeProfileMetadataParseResult invalid =
        ctm::ParseChromeProfileInfoCacheJson("{broken");
    Expect(!invalid.succeeded && invalid.entries.empty(),
           "malformed Chrome metadata should fail as a whole");
}

void TestCreationTimeRequiresAStableDigitValue() {
    const std::optional<std::string> string_value =
        ctm::ParseChromeProfileCreationTimeJson(
            R"({"profile":{"creation_time":"13370000000000000"}})");
    const std::optional<std::string> number_value =
        ctm::ParseChromeProfileCreationTimeJson(
            R"({"profile":{"creation_time":13370000000000000}})");
    Expect(string_value == "13370000000000000" &&
               number_value == string_value,
           "string and integer Chrome creation times should normalize identically");
    Expect(!ctm::ParseChromeProfileCreationTimeJson(
                R"({"profile":{"creation_time":"not-stable"}})")
                .has_value(),
           "a non-digit creation time should fail closed");
}

void TestPrivacyKeyIsStableButRejectsProfileRecreation() {
    wchar_t root_buffer[MAX_PATH]{};
    GetTempPathW(MAX_PATH, root_buffer);
    const std::filesystem::path root =
        std::filesystem::path(root_buffer) / L"Chrome User Data";
    const std::optional<std::string> first =
        ctm::BuildChromeProfilePersistenceKey(
            root, "Profile 1", "13370000000000000");
    std::wstring differently_cased = root.native();
    for (wchar_t& character : differently_cased) {
        character = static_cast<wchar_t>(std::towupper(character));
    }
    const std::optional<std::string> same =
        ctm::BuildChromeProfilePersistenceKey(
            differently_cased, "profile 1", "13370000000000000");
    const std::optional<std::string> recreated =
        ctm::BuildChromeProfilePersistenceKey(
            root, "Profile 1", "13370000000000001");
    const std::optional<std::string> other_profile =
        ctm::BuildChromeProfilePersistenceKey(
            root, "Profile 2", "13370000000000000");
    Expect(first.has_value() && first->size() == 64U && first == same,
           "the opaque SHA-256 key should be stable across Windows path casing");
    Expect(recreated.has_value() && recreated != first &&
               other_profile.has_value() && other_profile != first,
           "profile recreation and another directory must receive new keys");
    Expect(!ctm::BuildChromeProfilePersistenceKey(
                L"relative", "Profile 1", "13370000000000000")
                .has_value(),
           "a relative user-data path should be rejected");
}

}  // namespace

int RunLiveReadOnlyProbe() {
    const HRESULT initialized =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) {
        std::cerr << "Live probe COM initialization failed.\n";
        return 2;
    }
    const ctm::ChromeWindowEnumerationResult enumeration =
        ctm::EnumerateChromeWindows();
    ctm::ChromeProfileResolver resolver;
    std::size_t manageable = 0;
    std::size_t matched = 0;
    for (const ctm::ChromeWindowRecord& record : enumeration.chrome_windows) {
        if (!record.assessment.manageable) {
            continue;
        }
        ++manageable;
        const ctm::ChromeProfileResolution result =
            resolver.Resolve(record.snapshot);
        std::wcout << L"window_index=" << manageable << L" status="
                   << ctm::ChromeProfileResolutionStatusName(result.status)
                   << L'\n';
        if (result.matched()) {
            ++matched;
        }
    }
    resolver.ClearCache();
    CoUninitialize();
    std::cout << "manageable=" << manageable << " matched=" << matched
              << '\n';
    return enumeration.succeeded && manageable != 0 && matched == manageable
               ? 0
               : 3;
}

int main(const int argc, const char* const argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--live-read-only") {
        return RunLiveReadOnlyProbe();
    }
    TestInfoCacheParsingPreservesUnicodeAndSafetyFlags();
    TestCreationTimeRequiresAStableDigitValue();
    TestPrivacyKeyIsStableButRejectsProfileRecreation();

    if (failures != 0) {
        std::cerr << failures << " Chrome profile resolver test(s) failed.\n";
        return 1;
    }
    std::cout << "All Chrome profile resolver tests passed.\n";
    return 0;
}
