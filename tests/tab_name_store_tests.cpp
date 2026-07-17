#include "tab_name_store.h"

#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void Expect(const bool condition, const std::string_view description) {
    if (condition) {
        return;
    }
    std::cerr << "FAILED: " << description << '\n';
    ++failures;
}

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                (L"ChromeTaskbarMergerTabNames-" +
                 std::to_wstring(GetCurrentProcessId()) + L"-" +
                 std::to_wstring(GetTickCount64()));
        std::error_code error;
        std::filesystem::create_directories(path_, error);
        created_ = !error;
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }
    [[nodiscard]] bool created() const noexcept {
        return created_;
    }

private:
    std::filesystem::path path_;
    bool created_ = false;
};

[[nodiscard]] ctm::TabNameRule MakeRule(
    std::string id,
    std::wstring title,
    std::wstring display) {
    return {
        .id = std::move(id),
        .process_path =
            L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        .class_name = L"Chrome_WidgetWin_1",
        .exact_window_title = std::move(title),
        .display_name = std::move(display),
    };
}

[[nodiscard]] ctm::ChromeWindowSnapshot MakeWindow(
    std::wstring title) {
    ctm::ChromeWindowSnapshot window;
    window.process_path =
        L"C:\\Program Files\\Google\\Chrome\\Application\\CHROME.EXE";
    window.class_name = L"chrome_widgetwin_1";
    window.title = std::move(title);
    return window;
}

void TestVersionedRoundTripAndAtomicPersistence() {
    const std::vector rules = {
        MakeRule("work", L"Work - Google Chrome", L"工作"),
        MakeRule("mail", L"Mail - Google Chrome", L"邮件"),
    };
    const std::string serialized = ctm::SerializeTabNameRules(rules);
    const ctm::TabNameParseResult parsed =
        ctm::ParseTabNameRules(serialized);
    Expect(parsed.succeeded && parsed.rules.size() == 2,
           "versioned custom tab names should round-trip");
    if (parsed.rules.size() == 2) {
        Expect(parsed.rules[0].display_name == L"工作" &&
                   parsed.rules[1].id == "mail",
               "UTF-8 names and stable rule ids should survive parsing");
    }

    TemporaryDirectory directory;
    if (!directory.created()) {
        return;
    }
    const std::filesystem::path path =
        directory.path() / L"tab-names-v1.tsv";
    std::wstring error;
    Expect(ctm::SaveTabNameRulesAtomically(path, rules, &error),
           "valid tab-name rules should save atomically");
    const ctm::TabNameLoadResult loaded = ctm::LoadTabNameRules(path);
    Expect(loaded.succeeded && loaded.file_found &&
               loaded.rules.size() == 2,
           "an atomically saved rule file should reload completely");
}

void TestInvalidAndDuplicateRulesAreRejectedAsAWhole() {
    const ctm::TabNameParseResult unknown = ctm::ParseTabNameRules(
        "ChromeTaskbarMergerTabNames\t2\n");
    Expect(!unknown.succeeded && unknown.rules.empty(),
           "an unknown tab-name version should be rejected");
    const std::string duplicate =
        "ChromeTaskbarMergerTabNames\t1\n"
        "rule\tsame\tC:\\\\chrome.exe\tChrome_WidgetWin_1\tOne\tA\n"
        "rule\tsame\tC:\\\\chrome.exe\tChrome_WidgetWin_1\tTwo\tB\n";
    const ctm::TabNameParseResult parsed =
        ctm::ParseTabNameRules(duplicate);
    Expect(!parsed.succeeded && parsed.rules.empty(),
           "duplicate stable ids should reject the entire file");
}

void TestConservativeMatchingNeverUsesHwndOrAmbiguousRules() {
    const std::vector windows = {
        MakeWindow(L"Work - Google Chrome"),
        MakeWindow(L"Mail - Google Chrome"),
    };
    const std::vector rules = {
        MakeRule("work", L"Work - Google Chrome", L"工作"),
        MakeRule("mail", L"Mail - Google Chrome", L"邮件"),
    };
    std::vector<std::wstring> names =
        ctm::ResolveTabDisplayNames(rules, windows);
    Expect(names.size() == 2 && names[0] == L"工作" &&
               names[1] == L"邮件",
           "a unique stable path/class/title rule should apply its name");

    std::vector duplicate_windows = windows;
    duplicate_windows.push_back(MakeWindow(L"Work - Google Chrome"));
    names = ctm::ResolveTabDisplayNames(rules, duplicate_windows);
    Expect(names[0] == L"Work - Google Chrome" &&
               names[2] == L"Work - Google Chrome",
           "one rule matching multiple windows should be ignored as ambiguous");

    const std::vector colliding_rules = {
        MakeRule("work-a", L"Work - Google Chrome", L"工作 A"),
        MakeRule("work-b", L"Work - Google Chrome", L"工作 B"),
    };
    names = ctm::ResolveTabDisplayNames(
        colliding_rules,
        std::span<const ctm::ChromeWindowSnapshot>(windows.data(), 1));
    Expect(names[0] == L"Work - Google Chrome",
           "multiple rules targeting one window should all be ignored");
}

}  // namespace

int main() {
    TestVersionedRoundTripAndAtomicPersistence();
    TestInvalidAndDuplicateRulesAreRejectedAsAWhole();
    TestConservativeMatchingNeverUsesHwndOrAmbiguousRules();

    if (failures != 0) {
        std::cerr << failures << " tab-name store test(s) failed.\n";
        return 1;
    }
    std::cout << "All tab-name store tests passed.\n";
    return 0;
}
