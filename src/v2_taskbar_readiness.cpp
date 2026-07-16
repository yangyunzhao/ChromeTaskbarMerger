#include "v2_taskbar_readiness.h"

namespace ctm {

bool TabGroupTaskbarReadinessGate::ConfirmReadyAfterRecoveryWrite(
    const WindowIdentity& identity,
    std::wstring* const error_message) {
    if (model_ == nullptr ||
        !model_->MarkRecoveryIntentPersisted(identity, true)) {
        if (error_message != nullptr) {
            *error_message =
                L"The recovery intent does not map to a current tab identity.";
        }
        return false;
    }
    if (!model_->CanRemoveFromTaskbar(identity)) {
        if (error_message != nullptr) {
            *error_message =
                L"The tab, strip health, activation path, and recovery intent are not all ready.";
        }
        return false;
    }
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

void TabGroupTaskbarReadinessGate::RecoveryIntentCleared(
    const WindowIdentity& identity) noexcept {
    if (model_ != nullptr) {
        static_cast<void>(
            model_->MarkRecoveryIntentPersisted(identity, false));
    }
}

}  // namespace ctm
