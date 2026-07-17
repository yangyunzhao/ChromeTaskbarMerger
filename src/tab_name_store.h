#pragma once

#include "chrome_window.h"
#include "window_identity.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ctm {

struct TabNameRule {
    std::string id;
    std::wstring process_path;
    std::wstring class_name;
    std::wstring exact_window_title;
    std::wstring display_name;
};

struct TabNameParseResult {
    bool succeeded = false;
    std::vector<TabNameRule> rules;
    std::wstring error_message;
};

struct TabNameLoadResult {
    bool succeeded = false;
    bool file_found = false;
    std::vector<TabNameRule> rules;
    std::wstring error_message;
};

inline constexpr std::size_t kMaximumInMemoryTabNameLength = 128;

enum class InMemoryTabNameUpdateResult {
    Stored,
    Cleared,
    InvalidIdentity,
    TooLong,
    InvalidText,
};

class InMemoryTabNameStore final {
public:
    [[nodiscard]] InMemoryTabNameUpdateResult Set(
        const WindowIdentity& identity,
        std::wstring_view name);
    [[nodiscard]] std::optional<std::wstring> Find(
        const WindowIdentity& identity) const;
    [[nodiscard]] std::wstring Resolve(
        const WindowIdentity& identity,
        std::wstring_view fallback) const;
    [[nodiscard]] bool Remove(const WindowIdentity& identity) noexcept;
    void Clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

private:
    struct Entry {
        WindowIdentity identity;
        std::wstring name;
    };

    std::vector<Entry> entries_;
};

struct ProfileTabNameEntry {
    std::string profile_key;
    std::wstring display_name;
};

struct ProfileTabNameParseResult {
    bool succeeded = false;
    std::vector<ProfileTabNameEntry> entries;
    std::wstring error_message;
};

struct ProfileTabNameLoadResult {
    bool succeeded = false;
    bool file_found = false;
    std::vector<ProfileTabNameEntry> entries;
    std::wstring error_message;
};

class ProfileTabNameStore final {
public:
    [[nodiscard]] bool Replace(
        std::vector<ProfileTabNameEntry> entries) noexcept;
    [[nodiscard]] InMemoryTabNameUpdateResult Set(
        std::string_view profile_key,
        std::wstring_view name);
    [[nodiscard]] std::optional<std::wstring> Find(
        std::string_view profile_key) const;
    [[nodiscard]] std::wstring Resolve(
        std::string_view profile_key,
        std::wstring_view fallback) const;

    [[nodiscard]] std::span<const ProfileTabNameEntry> entries() const noexcept {
        return entries_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

private:
    std::vector<ProfileTabNameEntry> entries_;
};

[[nodiscard]] std::string SerializeTabNameRules(
    std::span<const TabNameRule> rules);
[[nodiscard]] TabNameParseResult ParseTabNameRules(
    std::string_view serialized);
[[nodiscard]] TabNameLoadResult LoadTabNameRules(
    const std::filesystem::path& path);
[[nodiscard]] bool SaveTabNameRulesAtomically(
    const std::filesystem::path& path,
    std::span<const TabNameRule> rules,
    std::wstring* error_message);

[[nodiscard]] std::vector<std::wstring> ResolveTabDisplayNames(
    std::span<const TabNameRule> rules,
    std::span<const ChromeWindowSnapshot> windows);

[[nodiscard]] std::string SerializeProfileTabNames(
    std::span<const ProfileTabNameEntry> entries);
[[nodiscard]] ProfileTabNameParseResult ParseProfileTabNames(
    std::string_view serialized);
[[nodiscard]] ProfileTabNameLoadResult LoadProfileTabNames(
    const std::filesystem::path& path);
[[nodiscard]] bool SaveProfileTabNamesAtomically(
    const std::filesystem::path& path,
    std::span<const ProfileTabNameEntry> entries,
    std::wstring* error_message);

}  // namespace ctm
