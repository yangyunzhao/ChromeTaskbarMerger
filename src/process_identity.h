#pragma once

#include <Windows.h>

#include <cstdint>

namespace ctm {

struct ProcessCreationTimeResult {
    std::uint64_t value = 0;
    bool succeeded = false;
    DWORD error_code = ERROR_SUCCESS;
};

[[nodiscard]] ProcessCreationTimeResult QueryProcessCreationTime(
    DWORD process_id);

}  // namespace ctm
