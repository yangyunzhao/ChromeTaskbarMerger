#include "process_identity.h"

namespace ctm {

ProcessCreationTimeResult QueryProcessCreationTime(const DWORD process_id) {
    ProcessCreationTimeResult result;
    const HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (process == nullptr) {
        result.error_code = GetLastError();
        return result;
    }

    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(
            process, &creation, &exit, &kernel, &user) == FALSE) {
        result.error_code = GetLastError();
        CloseHandle(process);
        return result;
    }
    CloseHandle(process);

    ULARGE_INTEGER value{};
    value.LowPart = creation.dwLowDateTime;
    value.HighPart = creation.dwHighDateTime;
    result.value = value.QuadPart;
    result.succeeded = result.value != 0;
    if (!result.succeeded) {
        result.error_code = ERROR_INVALID_DATA;
    }
    return result;
}

}  // namespace ctm
