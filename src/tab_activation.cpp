#include "tab_activation.h"

#include <utility>

namespace ctm {

TabActivationReport TabActivationCoordinator::Verify(
    const WindowIdentity& identity) {
    TabActivationReport report;
    if (model_ == nullptr || gateway_ == nullptr) {
        report.win32_error = ERROR_INVALID_PARAMETER;
        report.message = L"The tab activation dependencies are unavailable.";
        return report;
    }
    const TabGroupMember* const member = model_->FindMember(identity);
    if (member == nullptr || !member->reachability.tab_created) {
        report.win32_error = ERROR_NOT_READY;
        report.message = L"The target internal tab does not exist.";
        return report;
    }

    report.gateway_called = true;
    WindowActivationResult verification = gateway_->Verify(identity);
    report.win32_error = verification.win32_error;
    report.message = std::move(verification.message);
    if (!verification.succeeded) {
        static_cast<void>(
            model_->MarkActivationPathVerified(identity, false));
        return report;
    }
    if (!model_->MarkActivationPathVerified(identity, true)) {
        report.win32_error = ERROR_INVALID_STATE;
        report.message = L"The verified tab identity is no longer current.";
        return report;
    }

    report.succeeded = true;
    if (report.message.empty()) {
        report.message = L"The internal-tab activation path was verified.";
    }
    return report;
}

TabActivationReport TabActivationCoordinator::Activate(
    const WindowIdentity& identity) {
    TabActivationReport report;
    if (model_ == nullptr || gateway_ == nullptr) {
        report.win32_error = ERROR_INVALID_PARAMETER;
        report.message = L"The tab activation dependencies are unavailable.";
        return report;
    }
    if (!model_->CanActivate(identity)) {
        report.win32_error = ERROR_NOT_READY;
        report.message =
            L"The target does not have a verified internal-tab activation path.";
        return report;
    }

    report.gateway_called = true;
    WindowActivationResult activation = gateway_->Activate(identity);
    report.win32_error = activation.win32_error;
    report.message = std::move(activation.message);
    if (!activation.succeeded) {
        return report;
    }
    if (!model_->SetActive(identity)) {
        report.win32_error = ERROR_INVALID_STATE;
        report.message = L"The activated tab identity is no longer current.";
        return report;
    }

    report.succeeded = true;
    if (report.message.empty()) {
        report.message = L"The verified tab was activated.";
    }
    return report;
}

}  // namespace ctm
