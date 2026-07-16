#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace ctm {

struct WindowIdentity {
    HWND hwnd = nullptr;
    DWORD process_id = 0;
    DWORD thread_id = 0;
    std::uint64_t process_creation_time = 0;
    std::wstring class_name;
};

[[nodiscard]] bool WindowIdentityIsComplete(
    const WindowIdentity& identity) noexcept;
[[nodiscard]] bool WindowIdentityValuesMatch(
    const WindowIdentity& expected,
    DWORD process_id,
    DWORD thread_id,
    std::uint64_t process_creation_time,
    std::wstring_view class_name) noexcept;
[[nodiscard]] bool WindowIdentitiesMatch(
    const WindowIdentity& left,
    const WindowIdentity& right) noexcept;

}  // namespace ctm
