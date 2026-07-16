#pragma once

#include "window_identity.h"

#include <Windows.h>

namespace ctm {

struct WindowIdentityQueryResult {
    WindowIdentity identity;
    bool succeeded = false;
    bool window_exists = false;
    DWORD error_code = ERROR_SUCCESS;
};

[[nodiscard]] WindowIdentityQueryResult QueryWindowIdentity(HWND hwnd);

}  // namespace ctm
