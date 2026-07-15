#pragma once

#include <Windows.h>

#include <string_view>

namespace ctm {

enum class SingleInstanceStatus {
    Primary,
    Existing,
    Error,
};

class SingleInstanceGuard final {
public:
    SingleInstanceGuard() = default;
    ~SingleInstanceGuard();

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

    [[nodiscard]] SingleInstanceStatus Acquire(
        std::wstring_view name,
        DWORD* error_code);

    [[nodiscard]] bool is_primary() const noexcept {
        return status_ == SingleInstanceStatus::Primary;
    }

private:
    HANDLE mutex_ = nullptr;
    SingleInstanceStatus status_ = SingleInstanceStatus::Error;
};

}  // namespace ctm
