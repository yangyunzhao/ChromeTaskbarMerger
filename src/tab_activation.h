#pragma once

#include "tab_group_model.h"

#include <Windows.h>

#include <string>

namespace ctm {

struct WindowActivationResult {
    bool succeeded = false;
    DWORD win32_error = ERROR_SUCCESS;
    std::wstring message;
};

class IWindowActivationGateway {
public:
    virtual ~IWindowActivationGateway() = default;

    [[nodiscard]] virtual WindowActivationResult Activate(
        const WindowIdentity& identity) = 0;
};

struct TabActivationReport {
    bool succeeded = false;
    bool gateway_called = false;
    DWORD win32_error = ERROR_SUCCESS;
    std::wstring message;
};

class TabActivationCoordinator final {
public:
    TabActivationCoordinator(TabGroupModel* model,
                             IWindowActivationGateway* gateway) noexcept
        : model_(model), gateway_(gateway) {}

    [[nodiscard]] TabActivationReport Activate(
        const WindowIdentity& identity);

private:
    TabGroupModel* model_ = nullptr;
    IWindowActivationGateway* gateway_ = nullptr;
};

}  // namespace ctm
