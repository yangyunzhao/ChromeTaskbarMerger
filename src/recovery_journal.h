#pragma once

#include "taskbar_controller.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ctm {

class IRecoveryStateStore {
public:
    virtual ~IRecoveryStateStore() = default;

    [[nodiscard]] virtual bool Save(
        std::span<const TaskbarMutationState> states,
        std::wstring* error_message) = 0;
};

struct RecoveryParseResult {
    bool succeeded = false;
    std::vector<TaskbarMutationState> states;
    std::wstring error_message;
};

struct RecoveryLoadResult {
    bool succeeded = false;
    bool file_found = false;
    std::vector<TaskbarMutationState> states;
    std::wstring error_message;
};

[[nodiscard]] std::string SerializeRecoveryStates(
    std::span<const TaskbarMutationState> states);
[[nodiscard]] RecoveryParseResult ParseRecoveryStates(
    std::string_view serialized);

class RecoveryJournal final : public IRecoveryStateStore {
public:
    explicit RecoveryJournal(std::filesystem::path path)
        : path_(std::move(path)) {}

    [[nodiscard]] RecoveryLoadResult Load() const;
    [[nodiscard]] bool Save(
        std::span<const TaskbarMutationState> states,
        std::wstring* error_message) override;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

}  // namespace ctm
