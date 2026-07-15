#include "windowtabs_presence.h"

#include <TlHelp32.h>

#include <cwchar>

namespace ctm {

ProcessPresenceResult QueryWindowTabsPresence() {
    ProcessPresenceResult result;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        result.error_code = GetLastError();
        return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry) == FALSE) {
        result.error_code = GetLastError();
        CloseHandle(snapshot);
        return result;
    }

    do {
        if (_wcsicmp(entry.szExeFile, L"WindowTabs.exe") == 0) {
            result.running = true;
            break;
        }
    } while (Process32NextW(snapshot, &entry) != FALSE);

    CloseHandle(snapshot);
    result.query_succeeded = true;
    return result;
}

}  // namespace ctm
