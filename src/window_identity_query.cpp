#include "window_identity_query.h"

#include "process_identity.h"

#include <array>

namespace ctm {

WindowIdentityQueryResult QueryWindowIdentity(const HWND hwnd) {
    WindowIdentityQueryResult result;
    result.identity.hwnd = hwnd;
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        result.error_code = ERROR_INVALID_WINDOW_HANDLE;
        return result;
    }

    result.window_exists = true;
    SetLastError(ERROR_SUCCESS);
    result.identity.thread_id =
        GetWindowThreadProcessId(hwnd, &result.identity.process_id);
    if (result.identity.thread_id == 0) {
        result.error_code = GetLastError();
        if (result.error_code == ERROR_SUCCESS) {
            result.error_code = ERROR_GEN_FAILURE;
        }
        return result;
    }

    const ProcessCreationTimeResult creation_time =
        QueryProcessCreationTime(result.identity.process_id);
    if (!creation_time.succeeded) {
        result.error_code = creation_time.error_code;
        return result;
    }
    result.identity.process_creation_time = creation_time.value;

    std::array<wchar_t, 256> class_name{};
    SetLastError(ERROR_SUCCESS);
    const int copied = GetClassNameW(
        hwnd, class_name.data(), static_cast<int>(class_name.size()));
    if (copied <= 0) {
        result.error_code = GetLastError();
        if (result.error_code == ERROR_SUCCESS) {
            result.error_code = ERROR_GEN_FAILURE;
        }
        return result;
    }

    result.identity.class_name.assign(
        class_name.data(), static_cast<std::size_t>(copied));
    result.succeeded = true;
    return result;
}

}  // namespace ctm
