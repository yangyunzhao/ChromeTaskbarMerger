#include "tab_name_store.h"

#include <Windows.h>

#include <cstdint>
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

[[nodiscard]] ctm::WindowIdentity MakeIdentity(
    const std::uintptr_t handle,
    const DWORD process_id = 101,
    const DWORD thread_id = 201,
    const std::uint64_t creation_time = 301) {
    return {
        .hwnd = reinterpret_cast<HWND>(handle),
        .process_id = process_id,
        .thread_id = thread_id,
        .process_creation_time = creation_time,
        .class_name = L"Chrome_WidgetWin_1",
    };
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

void TestInMemoryNamesFollowCompleteWindowIdentity() {
    ctm::InMemoryTabNameStore names;
    const ctm::WindowIdentity work = MakeIdentity(0x101);
    Expect(names.Set(work, L"工作账户") ==
               ctm::InMemoryTabNameUpdateResult::Stored,
           "a Chinese custom name should be stored in memory");
    Expect(names.size() == 1 &&
               names.Resolve(work, L"Original title") == L"工作账户",
           "the in-memory name should override the current Chrome title");
    Expect(names.Resolve(work, L"Changed page title") == L"工作账户",
           "Chrome title changes should not overwrite the in-memory name");

    ctm::WindowIdentity reused = work;
    ++reused.process_creation_time;
    Expect(!names.Find(reused).has_value() &&
               names.Resolve(reused, L"New Chrome window") ==
                   L"New Chrome window",
           "a reused HWND with a different complete identity must not inherit a name");

    Expect(names.Set(work, L"") ==
               ctm::InMemoryTabNameUpdateResult::Cleared &&
               names.size() == 0 &&
               names.Resolve(work, L"Live title") == L"Live title",
           "an empty edit should clear the override and restore the live title");
}

void TestInMemoryNameValidationAndExplicitLifetime() {
    ctm::InMemoryTabNameStore names;
    const ctm::WindowIdentity identity = MakeIdentity(0x202);
    const std::wstring maximum(
        ctm::kMaximumInMemoryTabNameLength, L'名');
    const std::wstring too_long(
        ctm::kMaximumInMemoryTabNameLength + 1U, L'名');
    Expect(names.Set(identity, maximum) ==
               ctm::InMemoryTabNameUpdateResult::Stored,
           "the documented Unicode length limit should be accepted");
    Expect(names.Set(identity, too_long) ==
               ctm::InMemoryTabNameUpdateResult::TooLong &&
               names.Find(identity) == maximum,
           "an oversized edit should be rejected without replacing the old name");
    Expect(names.Set(identity, L"line one\nline two") ==
               ctm::InMemoryTabNameUpdateResult::InvalidText,
           "multi-line text should be rejected by the single-line name model");

    ctm::WindowIdentity incomplete = identity;
    incomplete.process_id = 0;
    Expect(names.Set(incomplete, L"invalid") ==
               ctm::InMemoryTabNameUpdateResult::InvalidIdentity,
           "an incomplete identity should never receive an in-memory name");
    Expect(names.Remove(identity) && names.size() == 0,
           "a name can be explicitly forgotten when its window identity expires");
    Expect(!names.Remove(identity),
           "removing an already absent identity should be idempotent");
    Expect(names.Set(identity, L"临时") ==
               ctm::InMemoryTabNameUpdateResult::Stored,
           "the store should accept another temporary name");
    names.Clear();
    Expect(names.size() == 0,
           "process shutdown can discard every in-memory name without persistence");
}

}  // namespace

int main() {
    TestVersionedRoundTripAndAtomicPersistence();
    TestInvalidAndDuplicateRulesAreRejectedAsAWhole();
    TestConservativeMatchingNeverUsesHwndOrAmbiguousRules();
    TestInMemoryNamesFollowCompleteWindowIdentity();
    TestInMemoryNameValidationAndExplicitLifetime();

    if (failures != 0) {
        std::cerr << failures << " tab-name store test(s) failed.\n";
        return 1;
    }
    std::cout << "All tab-name store tests passed.\n";
    return 0;
}
