#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace ctm {

class Logger final {
public:
    Logger() = default;
    ~Logger() = default;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    [[nodiscard]] bool Initialize(std::wstring* error_message);
    void Info(std::string_view message);
    void Info(std::wstring_view message);
    void Warning(std::string_view message);
    void Warning(std::wstring_view message);
    void Error(std::string_view message);
    void Error(std::wstring_view message);

    [[nodiscard]] const std::filesystem::path& log_path() const noexcept {
        return log_path_;
    }

    [[nodiscard]] std::filesystem::path log_directory() const {
        return log_path_.parent_path();
    }

private:
    void Write(std::string_view level, std::string_view message);

    std::mutex mutex_;
    std::ofstream stream_;
    std::filesystem::path log_path_;
};

}  // namespace ctm
