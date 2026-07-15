#include "single_instance.h"

#include <string>

namespace ctm {

SingleInstanceGuard::~SingleInstanceGuard() {
    if (mutex_ != nullptr) {
        CloseHandle(mutex_);
    }
}

SingleInstanceStatus SingleInstanceGuard::Acquire(
    const std::wstring_view name,
    DWORD* const error_code) {
    if (mutex_ != nullptr || name.empty()) {
        if (error_code != nullptr) {
            *error_code = ERROR_INVALID_PARAMETER;
        }
        status_ = SingleInstanceStatus::Error;
        return status_;
    }

    const std::wstring null_terminated(name);
    SetLastError(ERROR_SUCCESS);
    mutex_ = CreateMutexW(nullptr, FALSE, null_terminated.c_str());
    if (mutex_ == nullptr) {
        if (error_code != nullptr) {
            *error_code = GetLastError();
        }
        status_ = SingleInstanceStatus::Error;
        return status_;
    }

    const DWORD creation_error = GetLastError();
    status_ = creation_error == ERROR_ALREADY_EXISTS
                  ? SingleInstanceStatus::Existing
                  : SingleInstanceStatus::Primary;
    if (error_code != nullptr) {
        *error_code = ERROR_SUCCESS;
    }
    return status_;
}

}  // namespace ctm
