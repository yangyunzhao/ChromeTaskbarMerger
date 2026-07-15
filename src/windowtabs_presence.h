#pragma once

#include <Windows.h>

namespace ctm {

struct ProcessPresenceResult {
    bool query_succeeded = false;
    bool running = false;
    DWORD error_code = ERROR_SUCCESS;
};

[[nodiscard]] ProcessPresenceResult QueryWindowTabsPresence();

}  // namespace ctm
