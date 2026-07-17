#include "chrome_profile_resolver.h"

#include "window_identity.h"

#include <Windows.h>
#include <Ole2.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <UIAutomation.h>
#include <winternl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctm {
namespace {

constexpr std::size_t kMaximumChromeJsonBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumCommandLineBytes = 1024U * 1024U;
constexpr std::size_t kMaximumJsonDepth = 64;
constexpr std::size_t kMaximumResolverCacheEntries = 128;

enum class JsonKind {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
};

struct JsonValue {
    JsonKind kind = JsonKind::Null;
    bool boolean = false;
    std::string text;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;
};

void AppendUtf8(std::string* const output, const std::uint32_t code_point) {
    if (code_point <= 0x7FU) {
        output->push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        output->push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output->push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
        output->push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        output->push_back(
            static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output->push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        output->push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        output->push_back(
            static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        output->push_back(
            static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output->push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

class JsonParser final {
public:
    explicit JsonParser(const std::string_view input) : input_(input) {}

    [[nodiscard]] bool Parse(JsonValue* const output) {
        if (output == nullptr || input_.empty() ||
            input_.size() > kMaximumChromeJsonBytes) {
            return false;
        }
        SkipWhitespace();
        if (!ParseValue(output, 0)) {
            return false;
        }
        SkipWhitespace();
        return position_ == input_.size();
    }

private:
    void SkipWhitespace() noexcept {
        while (position_ < input_.size() &&
               (input_[position_] == ' ' || input_[position_] == '\t' ||
                input_[position_] == '\r' || input_[position_] == '\n')) {
            ++position_;
        }
    }

    [[nodiscard]] bool Consume(const char expected) noexcept {
        if (position_ >= input_.size() || input_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    [[nodiscard]] bool ParseHexQuad(std::uint32_t* const value) {
        if (value == nullptr || position_ + 4U > input_.size()) {
            return false;
        }
        std::uint32_t decoded = 0;
        for (std::size_t index = 0; index < 4; ++index) {
            const unsigned char character =
                static_cast<unsigned char>(input_[position_++]);
            decoded <<= 4U;
            if (character >= '0' && character <= '9') {
                decoded |= character - '0';
            } else if (character >= 'a' && character <= 'f') {
                decoded |= 10U + character - 'a';
            } else if (character >= 'A' && character <= 'F') {
                decoded |= 10U + character - 'A';
            } else {
                return false;
            }
        }
        *value = decoded;
        return true;
    }

    [[nodiscard]] bool ParseString(std::string* const output) {
        if (output == nullptr || !Consume('"')) {
            return false;
        }
        output->clear();
        while (position_ < input_.size()) {
            const unsigned char character =
                static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                return true;
            }
            if (character < 0x20U) {
                return false;
            }
            if (character != '\\') {
                output->push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) {
                return false;
            }
            const char escape = input_[position_++];
            switch (escape) {
                case '"': output->push_back('"'); break;
                case '\\': output->push_back('\\'); break;
                case '/': output->push_back('/'); break;
                case 'b': output->push_back('\b'); break;
                case 'f': output->push_back('\f'); break;
                case 'n': output->push_back('\n'); break;
                case 'r': output->push_back('\r'); break;
                case 't': output->push_back('\t'); break;
                case 'u': {
                    std::uint32_t code_point = 0;
                    if (!ParseHexQuad(&code_point)) {
                        return false;
                    }
                    if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
                        if (position_ + 2U > input_.size() ||
                            input_[position_] != '\\' ||
                            input_[position_ + 1U] != 'u') {
                            return false;
                        }
                        position_ += 2U;
                        std::uint32_t low = 0;
                        if (!ParseHexQuad(&low) || low < 0xDC00U ||
                            low > 0xDFFFU) {
                            return false;
                        }
                        code_point = 0x10000U +
                                     ((code_point - 0xD800U) << 10U) +
                                     (low - 0xDC00U);
                    } else if (code_point >= 0xDC00U &&
                               code_point <= 0xDFFFU) {
                        return false;
                    }
                    AppendUtf8(output, code_point);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    [[nodiscard]] bool ParseNumber(std::string* const output) {
        const std::size_t start = position_;
        if (position_ < input_.size() && input_[position_] == '-') {
            ++position_;
        }
        if (position_ >= input_.size()) {
            return false;
        }
        if (input_[position_] == '0') {
            ++position_;
        } else {
            const std::size_t digits = position_;
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
            if (digits == position_) {
                return false;
            }
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            const std::size_t digits = position_;
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
            if (digits == position_) {
                return false;
            }
        }
        if (position_ < input_.size() &&
            (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() &&
                (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            const std::size_t digits = position_;
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
            if (digits == position_) {
                return false;
            }
        }
        output->assign(input_.substr(start, position_ - start));
        return true;
    }

    [[nodiscard]] bool ParseValue(JsonValue* const output,
                                  const std::size_t depth) {
        if (output == nullptr || depth > kMaximumJsonDepth) {
            return false;
        }
        SkipWhitespace();
        if (position_ >= input_.size()) {
            return false;
        }
        if (input_[position_] == '"') {
            output->kind = JsonKind::String;
            return ParseString(&output->text);
        }
        if (input_[position_] == '{') {
            output->kind = JsonKind::Object;
            ++position_;
            SkipWhitespace();
            if (Consume('}')) {
                return true;
            }
            while (position_ < input_.size()) {
                std::string key;
                if (!ParseString(&key)) {
                    return false;
                }
                SkipWhitespace();
                if (!Consume(':')) {
                    return false;
                }
                JsonValue value;
                if (!ParseValue(&value, depth + 1U)) {
                    return false;
                }
                output->object.emplace_back(std::move(key), std::move(value));
                SkipWhitespace();
                if (Consume('}')) {
                    return true;
                }
                if (!Consume(',')) {
                    return false;
                }
                SkipWhitespace();
            }
            return false;
        }
        if (input_[position_] == '[') {
            output->kind = JsonKind::Array;
            ++position_;
            SkipWhitespace();
            if (Consume(']')) {
                return true;
            }
            while (position_ < input_.size()) {
                JsonValue value;
                if (!ParseValue(&value, depth + 1U)) {
                    return false;
                }
                output->array.push_back(std::move(value));
                SkipWhitespace();
                if (Consume(']')) {
                    return true;
                }
                if (!Consume(',')) {
                    return false;
                }
                SkipWhitespace();
            }
            return false;
        }
        if (input_.substr(position_, 4) == "true") {
            output->kind = JsonKind::Boolean;
            output->boolean = true;
            position_ += 4;
            return true;
        }
        if (input_.substr(position_, 5) == "false") {
            output->kind = JsonKind::Boolean;
            output->boolean = false;
            position_ += 5;
            return true;
        }
        if (input_.substr(position_, 4) == "null") {
            output->kind = JsonKind::Null;
            position_ += 4;
            return true;
        }
        output->kind = JsonKind::Number;
        return ParseNumber(&output->text);
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

[[nodiscard]] const JsonValue* FindMember(const JsonValue& object,
                                          const std::string_view name) {
    if (object.kind != JsonKind::Object) {
        return nullptr;
    }
    const auto member = std::find_if(
        object.object.begin(), object.object.end(),
        [name](const auto& item) { return item.first == name; });
    return member == object.object.end() ? nullptr : &member->second;
}

[[nodiscard]] std::wstring Utf8ToWide(const std::string_view value) {
    if (value.empty() || value.size() >
                             static_cast<std::size_t>(
                                 std::numeric_limits<int>::max())) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring decoded(static_cast<std::size_t>(required), L'\0');
    return MultiByteToWideChar(
               CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
               static_cast<int>(value.size()), decoded.data(), required) ==
               required
               ? decoded
               : std::wstring{};
}

[[nodiscard]] std::string WideToUtf8(const std::wstring_view value) {
    if (value.empty() || value.size() >
                             static_cast<std::size_t>(
                                 std::numeric_limits<int>::max())) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string encoded(static_cast<std::size_t>(required), '\0');
    return WideCharToMultiByte(
               CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
               static_cast<int>(value.size()), encoded.data(), required,
               nullptr, nullptr) == required
               ? encoded
               : std::string{};
}

[[nodiscard]] bool ProfileDirectoryIsSafe(const std::string_view value) {
    return !value.empty() && value.size() <= 255U && value != "." &&
           value != ".." && value.find_first_of("/\\\r\n\t") ==
                                std::string_view::npos;
}

[[nodiscard]] std::optional<std::string> ReadSharedFile(
    const std::filesystem::path& path) {
    const HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) == FALSE || size.QuadPart <= 0 ||
        size.QuadPart > static_cast<LONGLONG>(kMaximumChromeJsonBytes)) {
        CloseHandle(file);
        return std::nullopt;
    }
    std::string content(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t total = 0;
    while (total < content.size()) {
        DWORD read = 0;
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            content.size() - total, std::numeric_limits<DWORD>::max()));
        if (ReadFile(file, content.data() + total, requested, &read, nullptr) ==
                FALSE ||
            read == 0) {
            CloseHandle(file);
            return std::nullopt;
        }
        total += read;
    }
    CloseHandle(file);
    return content;
}

[[nodiscard]] WindowIdentity MakeWindowIdentity(
    const ChromeWindowSnapshot& window) {
    return {
        .hwnd = window.hwnd,
        .process_id = window.process_id,
        .thread_id = window.thread_id,
        .process_creation_time = window.process_creation_time,
        .class_name = window.class_name,
    };
}

using NtQueryInformationProcessPointer = LONG(NTAPI*)(
    HANDLE, ULONG, PVOID, ULONG, PULONG);

[[nodiscard]] std::optional<std::wstring> QueryProcessCommandLine(
    const DWORD process_id) {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto query = ntdll == nullptr
                           ? nullptr
                           : reinterpret_cast<NtQueryInformationProcessPointer>(
                                 GetProcAddress(ntdll, "NtQueryInformationProcess"));
    if (query == nullptr) {
        return std::nullopt;
    }
    const HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (process == nullptr) {
        return std::nullopt;
    }
    ULONG required = 0;
    constexpr ULONG kProcessCommandLineInformation = 60;
    static_cast<void>(query(
        process, kProcessCommandLineInformation, nullptr, 0, &required));
    if (required < sizeof(UNICODE_STRING) ||
        required > kMaximumCommandLineBytes) {
        CloseHandle(process);
        return std::nullopt;
    }
    std::vector<std::byte> buffer(required);
    const LONG status = query(
        process, kProcessCommandLineInformation, buffer.data(), required,
        &required);
    CloseHandle(process);
    if (status < 0 || required < sizeof(UNICODE_STRING)) {
        return std::nullopt;
    }
    const auto* command =
        reinterpret_cast<const UNICODE_STRING*>(buffer.data());
    if (command->Buffer == nullptr || command->Length == 0 ||
        command->Length % sizeof(wchar_t) != 0 ||
        command->Length > required - sizeof(UNICODE_STRING)) {
        return std::nullopt;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(buffer.data());
    const auto end = begin + buffer.size();
    const auto text_begin = reinterpret_cast<std::uintptr_t>(command->Buffer);
    if (text_begin < begin || text_begin + command->Length > end) {
        return std::nullopt;
    }
    return std::wstring(
        command->Buffer, command->Length / sizeof(wchar_t));
}

[[nodiscard]] std::optional<std::filesystem::path> DefaultUserDataDirectory(
    const std::wstring_view process_path) {
    std::wstring lowered(process_path);
    std::transform(
        lowered.begin(), lowered.end(), lowered.begin(),
        [](const wchar_t character) {
            return static_cast<wchar_t>(std::towlower(character));
        });
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }
    std::wstring local_app_data(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(
        L"LOCALAPPDATA", local_app_data.data(), required);
    if (written == 0 || written >= required) {
        return std::nullopt;
    }
    local_app_data.resize(written);
    const std::filesystem::path root(local_app_data);
    if (lowered.find(L"\\google\\chrome beta\\application\\") !=
        std::wstring::npos) {
        return root / L"Google" / L"Chrome Beta" / L"User Data";
    }
    if (lowered.find(L"\\google\\chrome dev\\application\\") !=
        std::wstring::npos) {
        return root / L"Google" / L"Chrome Dev" / L"User Data";
    }
    if (lowered.find(L"\\google\\chrome sxs\\application\\") !=
        std::wstring::npos) {
        return root / L"Google" / L"Chrome SxS" / L"User Data";
    }
    if (lowered.find(L"\\google\\chrome for testing\\application\\") !=
        std::wstring::npos) {
        return root / L"Google" / L"Chrome for Testing" / L"User Data";
    }
    if (lowered.find(L"\\google\\chrome\\application\\") !=
        std::wstring::npos) {
        return root / L"Google" / L"Chrome" / L"User Data";
    }
    if (lowered.find(L"\\chromium\\application\\") !=
        std::wstring::npos) {
        return root / L"Chromium" / L"User Data";
    }
    return std::nullopt;
}

struct UserDataDirectoryResult {
    ChromeProfileResolutionStatus status =
        ChromeProfileResolutionStatus::UserDataDirectoryUnavailable;
    std::filesystem::path path;
};

[[nodiscard]] UserDataDirectoryResult ResolveUserDataDirectory(
    const ChromeWindowSnapshot& window) {
    const std::optional<std::wstring> command_line =
        QueryProcessCommandLine(window.process_id);
    if (!command_line.has_value()) {
        return {.status =
                    ChromeProfileResolutionStatus::CommandLineUnavailable};
    }
    int argument_count = 0;
    LPWSTR* const arguments = CommandLineToArgvW(
        command_line->c_str(), &argument_count);
    if (arguments == nullptr || argument_count <= 0) {
        if (arguments != nullptr) {
            LocalFree(arguments);
        }
        return {.status =
                    ChromeProfileResolutionStatus::CommandLineUnavailable};
    }
    std::optional<std::wstring> explicit_directory;
    constexpr std::wstring_view prefix = L"--user-data-dir=";
    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view argument(arguments[index]);
        if (argument.starts_with(prefix)) {
            explicit_directory = std::wstring(argument.substr(prefix.size()));
            break;
        }
        if (argument == L"--user-data-dir" && index + 1 < argument_count) {
            explicit_directory = arguments[index + 1];
            break;
        }
    }
    LocalFree(arguments);
    if (explicit_directory.has_value()) {
        std::filesystem::path path(*explicit_directory);
        if (path.empty() || !path.is_absolute()) {
            return {.status = ChromeProfileResolutionStatus::
                                  UserDataDirectoryUnavailable};
        }
        return {
            .status = ChromeProfileResolutionStatus::Matched,
            .path = path.lexically_normal(),
        };
    }
    const std::optional<std::filesystem::path> default_directory =
        DefaultUserDataDirectory(window.process_path);
    if (!default_directory.has_value()) {
        return {.status =
                    ChromeProfileResolutionStatus::UnsupportedChromeInstall};
    }
    return {
        .status = ChromeProfileResolutionStatus::Matched,
        .path = default_directory->lexically_normal(),
    };
}

struct AvatarNameResult {
    ChromeProfileResolutionStatus status =
        ChromeProfileResolutionStatus::UiAutomationUnavailable;
    std::wstring name;
};

[[nodiscard]] AvatarNameResult QueryAvatarAccessibleName(const HWND hwnd) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IUIAutomation> automation;
    if (FAILED(CoCreateInstance(
            CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&automation)))) {
        return {};
    }
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(hwnd, &root)) || root == nullptr) {
        return {};
    }
    VARIANT value{};
    VariantInit(&value);
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(L"AvatarToolbarButton");
    if (value.bstrVal == nullptr) {
        return {};
    }
    ComPtr<IUIAutomationCondition> condition;
    const HRESULT condition_result = automation->CreatePropertyCondition(
        UIA_ClassNamePropertyId, value, &condition);
    VariantClear(&value);
    if (FAILED(condition_result) || condition == nullptr) {
        return {};
    }
    ComPtr<IUIAutomationElementArray> matches;
    if (FAILED(root->FindAll(TreeScope_Descendants, condition.Get(), &matches)) ||
        matches == nullptr) {
        return {};
    }
    int count = 0;
    if (FAILED(matches->get_Length(&count))) {
        return {};
    }
    std::vector<std::wstring> visible_names;
    for (int index = 0; index < count; ++index) {
        ComPtr<IUIAutomationElement> element;
        if (FAILED(matches->GetElement(index, &element)) || element == nullptr) {
            continue;
        }
        CONTROLTYPEID control_type = 0;
        BOOL offscreen = TRUE;
        if (FAILED(element->get_CurrentControlType(&control_type)) ||
            control_type != UIA_ButtonControlTypeId ||
            FAILED(element->get_CurrentIsOffscreen(&offscreen)) || offscreen) {
            continue;
        }
        BSTR name = nullptr;
        if (SUCCEEDED(element->get_CurrentName(&name)) && name != nullptr &&
            SysStringLen(name) != 0) {
            visible_names.emplace_back(name, SysStringLen(name));
        }
        SysFreeString(name);
    }
    if (visible_names.size() != 1) {
        return {
            .status = visible_names.empty()
                          ? ChromeProfileResolutionStatus::AvatarUnavailable
                          : ChromeProfileResolutionStatus::AmbiguousProfile,
        };
    }
    return {
        .status = ChromeProfileResolutionStatus::Matched,
        .name = std::move(visible_names.front()),
    };
}

[[nodiscard]] ChromeProfileResolution ResolveUncached(
    const ChromeWindowSnapshot& window) {
    if (!WindowIdentityIsComplete(MakeWindowIdentity(window))) {
        return {.status =
                    ChromeProfileResolutionStatus::InvalidWindowIdentity};
    }
    const UserDataDirectoryResult user_data =
        ResolveUserDataDirectory(window);
    if (user_data.status != ChromeProfileResolutionStatus::Matched) {
        return {.status = user_data.status};
    }
    const std::optional<std::string> local_state =
        ReadSharedFile(user_data.path / L"Local State");
    if (!local_state.has_value()) {
        return {.status =
                    ChromeProfileResolutionStatus::LocalStateUnavailable};
    }
    const ChromeProfileMetadataParseResult metadata =
        ParseChromeProfileInfoCacheJson(*local_state);
    if (!metadata.succeeded) {
        return {.status = ChromeProfileResolutionStatus::MetadataInvalid};
    }
    const AvatarNameResult avatar = QueryAvatarAccessibleName(window.hwnd);
    if (avatar.status != ChromeProfileResolutionStatus::Matched) {
        return {.status = avatar.status};
    }
    std::vector<const ChromeProfileMetadataEntry*> matches;
    for (const ChromeProfileMetadataEntry& entry : metadata.entries) {
        if (entry.display_name == avatar.name) {
            matches.push_back(&entry);
        }
    }
    if (matches.empty()) {
        return {.status =
                    ChromeProfileResolutionStatus::NoMatchingProfile};
    }
    if (matches.size() != 1) {
        return {.status = ChromeProfileResolutionStatus::AmbiguousProfile};
    }
    if (matches.front()->is_ephemeral) {
        return {.status = ChromeProfileResolutionStatus::EphemeralProfile};
    }
    const std::optional<std::string> preferences = ReadSharedFile(
        user_data.path / Utf8ToWide(matches.front()->directory) /
        L"Preferences");
    if (!preferences.has_value()) {
        return {.status =
                    ChromeProfileResolutionStatus::CreationTimeUnavailable};
    }
    const std::optional<std::string> creation_time =
        ParseChromeProfileCreationTimeJson(*preferences);
    if (!creation_time.has_value()) {
        return {.status =
                    ChromeProfileResolutionStatus::CreationTimeUnavailable};
    }
    const std::optional<std::string> key = BuildChromeProfilePersistenceKey(
        user_data.path, matches.front()->directory, *creation_time);
    if (!key.has_value()) {
        return {.status = ChromeProfileResolutionStatus::HashUnavailable};
    }
    return {
        .status = ChromeProfileResolutionStatus::Matched,
        .profile_key = *key,
    };
}

}  // namespace

ChromeProfileMetadataParseResult ParseChromeProfileInfoCacheJson(
    const std::string_view json) {
    ChromeProfileMetadataParseResult result;
    JsonValue root;
    JsonParser parser(json);
    if (!parser.Parse(&root)) {
        return result;
    }
    const JsonValue* const profile = FindMember(root, "profile");
    const JsonValue* const info_cache =
        profile == nullptr ? nullptr : FindMember(*profile, "info_cache");
    if (info_cache == nullptr || info_cache->kind != JsonKind::Object) {
        return result;
    }
    for (const auto& [directory, value] : info_cache->object) {
        if (!ProfileDirectoryIsSafe(directory) ||
            value.kind != JsonKind::Object) {
            continue;
        }
        const JsonValue* const name = FindMember(value, "name");
        if (name == nullptr || name->kind != JsonKind::String) {
            continue;
        }
        const std::wstring decoded_name = Utf8ToWide(name->text);
        if (decoded_name.empty() || decoded_name.size() > 512U) {
            continue;
        }
        const JsonValue* const ephemeral =
            FindMember(value, "is_ephemeral");
        result.entries.push_back({
            .directory = directory,
            .display_name = decoded_name,
            .is_ephemeral = ephemeral != nullptr &&
                            ephemeral->kind == JsonKind::Boolean &&
                            ephemeral->boolean,
        });
    }
    result.succeeded = true;
    return result;
}

std::optional<std::string> ParseChromeProfileCreationTimeJson(
    const std::string_view json) {
    JsonValue root;
    JsonParser parser(json);
    if (!parser.Parse(&root)) {
        return std::nullopt;
    }
    const JsonValue* const profile = FindMember(root, "profile");
    const JsonValue* const creation_time =
        profile == nullptr ? nullptr : FindMember(*profile, "creation_time");
    if (creation_time == nullptr ||
        (creation_time->kind != JsonKind::String &&
         creation_time->kind != JsonKind::Number) ||
        creation_time->text.empty() || creation_time->text.size() > 64U ||
        !std::all_of(
            creation_time->text.begin(), creation_time->text.end(),
            [](const unsigned char character) {
                return character >= '0' && character <= '9';
            })) {
        return std::nullopt;
    }
    return creation_time->text;
}

std::optional<std::string> BuildChromeProfilePersistenceKey(
    const std::filesystem::path& user_data_directory,
    const std::string_view profile_directory,
    const std::string_view creation_time) {
    if (user_data_directory.empty() ||
        !user_data_directory.is_absolute() ||
        !ProfileDirectoryIsSafe(profile_directory) || creation_time.empty() ||
        !std::all_of(
            creation_time.begin(), creation_time.end(),
            [](const unsigned char character) {
                return character >= '0' && character <= '9';
            })) {
        return std::nullopt;
    }
    std::wstring normalized =
        user_data_directory.lexically_normal().native();
    std::transform(
        normalized.begin(), normalized.end(), normalized.begin(),
        [](const wchar_t character) {
            return static_cast<wchar_t>(std::towlower(character));
        });
    std::string material = WideToUtf8(normalized);
    if (material.empty()) {
        return std::nullopt;
    }
    std::wstring normalized_profile = Utf8ToWide(profile_directory);
    if (normalized_profile.empty()) {
        return std::nullopt;
    }
    std::transform(
        normalized_profile.begin(), normalized_profile.end(),
        normalized_profile.begin(), [](const wchar_t character) {
            return static_cast<wchar_t>(std::towlower(character));
        });
    const std::string normalized_profile_utf8 =
        WideToUtf8(normalized_profile);
    if (normalized_profile_utf8.empty()) {
        return std::nullopt;
    }
    material.push_back('\0');
    material.append(normalized_profile_utf8);
    material.push_back('\0');
    material.append(creation_time);

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD copied = 0;
    std::array<UCHAR, 32> digest{};
    if (BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
            &copied, 0) < 0 ||
        object_size == 0) {
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
        return std::nullopt;
    }
    std::vector<UCHAR> object(object_size);
    const bool succeeded =
        BCryptCreateHash(
            algorithm, &hash, object.data(), object_size, nullptr, 0, 0) >= 0 &&
        BCryptHashData(
            hash, reinterpret_cast<PUCHAR>(material.data()),
            static_cast<ULONG>(material.size()), 0) >= 0 &&
        BCryptFinishHash(
            hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!succeeded) {
        return std::nullopt;
    }
    constexpr char hexadecimal[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(digest.size() * 2U);
    for (const UCHAR byte : digest) {
        encoded.push_back(hexadecimal[byte >> 4U]);
        encoded.push_back(hexadecimal[byte & 0x0FU]);
    }
    return encoded;
}

std::wstring_view ChromeProfileResolutionStatusName(
    const ChromeProfileResolutionStatus status) noexcept {
    switch (status) {
        case ChromeProfileResolutionStatus::Matched: return L"matched";
        case ChromeProfileResolutionStatus::InvalidWindowIdentity:
            return L"invalid-window-identity";
        case ChromeProfileResolutionStatus::CommandLineUnavailable:
            return L"command-line-unavailable";
        case ChromeProfileResolutionStatus::UserDataDirectoryUnavailable:
            return L"user-data-directory-unavailable";
        case ChromeProfileResolutionStatus::UnsupportedChromeInstall:
            return L"unsupported-chrome-install";
        case ChromeProfileResolutionStatus::LocalStateUnavailable:
            return L"local-state-unavailable";
        case ChromeProfileResolutionStatus::MetadataInvalid:
            return L"metadata-invalid";
        case ChromeProfileResolutionStatus::UiAutomationUnavailable:
            return L"uia-unavailable";
        case ChromeProfileResolutionStatus::AvatarUnavailable:
            return L"avatar-unavailable";
        case ChromeProfileResolutionStatus::NoMatchingProfile:
            return L"profile-not-matched";
        case ChromeProfileResolutionStatus::AmbiguousProfile:
            return L"profile-ambiguous";
        case ChromeProfileResolutionStatus::EphemeralProfile:
            return L"profile-ephemeral";
        case ChromeProfileResolutionStatus::CreationTimeUnavailable:
            return L"creation-time-unavailable";
        case ChromeProfileResolutionStatus::HashUnavailable:
            return L"hash-unavailable";
    }
    return L"unknown";
}

ChromeProfileResolution ChromeProfileResolver::Resolve(
    const ChromeWindowSnapshot& window) const {
    const WindowIdentity identity = MakeWindowIdentity(window);
    const auto cached = std::find_if(
        cache_.begin(), cache_.end(),
        [&identity](const CacheEntry& entry) {
            return WindowIdentitiesMatch(entry.identity, identity);
        });
    if (cached != cache_.end()) {
        return cached->resolution;
    }
    if (cache_.size() >= kMaximumResolverCacheEntries) {
        cache_.clear();
    }
    ChromeProfileResolution resolution = ResolveUncached(window);
    // Chrome may expose the avatar element shortly after its top-level window
    // appears. Cache only verified matches so a transient UIA/metadata failure
    // can recover on a later synchronization without restarting the process.
    if (resolution.matched()) {
        cache_.push_back({
            .identity = identity,
            .resolution = resolution,
        });
    }
    return resolution;
}

void ChromeProfileResolver::ClearCache() noexcept {
    cache_.clear();
}

}  // namespace ctm
