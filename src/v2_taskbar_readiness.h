#pragma once

#include "fixed_entry_manager.h"
#include "tab_group_model.h"

namespace ctm {

class TabGroupTaskbarReadinessGate final
    : public IFixedEntryReadinessGate {
public:
    explicit TabGroupTaskbarReadinessGate(TabGroupModel* model) noexcept
        : model_(model) {}

    [[nodiscard]] bool ConfirmReadyAfterRecoveryWrite(
        const WindowIdentity& identity,
        std::wstring* error_message) override;
    void RecoveryIntentCleared(
        const WindowIdentity& identity) noexcept override;

private:
    TabGroupModel* model_ = nullptr;
};

}  // namespace ctm
