#pragma once

#include "chrome_window.h"
#include "window_identity.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ctm {

struct ChromeProfileMetadataEntry {
    std::string directory;
    std::wstring display_name;
    bool is_ephemeral = false;
};

struct ChromeProfileMetadataParseResult {
    bool succeeded = false;
    std::vector<ChromeProfileMetadataEntry> entries;
};

enum class ChromeProfileResolutionStatus {
    Matched,
    InvalidWindowIdentity,
    CommandLineUnavailable,
    UserDataDirectoryUnavailable,
    UnsupportedChromeInstall,
    LocalStateUnavailable,
    MetadataInvalid,
    UiAutomationUnavailable,
    AvatarUnavailable,
    NoMatchingProfile,
    AmbiguousProfile,
    EphemeralProfile,
    CreationTimeUnavailable,
    HashUnavailable,
};

struct ChromeProfileResolution {
    ChromeProfileResolutionStatus status =
        ChromeProfileResolutionStatus::InvalidWindowIdentity;
    std::string profile_key;

    [[nodiscard]] bool matched() const noexcept {
        return status == ChromeProfileResolutionStatus::Matched &&
               !profile_key.empty();
    }
};

// These parsing and key-building seams are public so profile persistence can
// be verified without reading or modifying a real Chrome profile.
[[nodiscard]] ChromeProfileMetadataParseResult
ParseChromeProfileInfoCacheJson(std::string_view json);
[[nodiscard]] std::optional<std::string>
ParseChromeProfileCreationTimeJson(std::string_view json);
[[nodiscard]] std::optional<std::string> BuildChromeProfilePersistenceKey(
    const std::filesystem::path& user_data_directory,
    std::string_view profile_directory,
    std::string_view creation_time);

[[nodiscard]] std::wstring_view ChromeProfileResolutionStatusName(
    ChromeProfileResolutionStatus status) noexcept;

class ChromeProfileResolver final {
public:
    [[nodiscard]] ChromeProfileResolution Resolve(
        const ChromeWindowSnapshot& window) const;
    void ClearCache() noexcept;

private:
    struct CacheEntry {
        WindowIdentity identity;
        ChromeProfileResolution resolution;
    };
    mutable std::vector<CacheEntry> cache_;
};

}  // namespace ctm
