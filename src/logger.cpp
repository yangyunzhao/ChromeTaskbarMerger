#include "logger.h"

#include <Windows.h>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

namespace ctm {
namespace {

[[nodiscard]] std::wstring ReadEnvironmentVariable(const wchar_t* name) {
    const DWORD required_size = GetEnvironmentVariableW(name, nullptr, 0);
    if (required_size == 0) {
        return {};
    }

    std::vector<wchar_t> buffer(required_size);
    const DWORD written =
        GetEnvironmentVariableW(name, buffer.data(), required_size);
    if (written == 0 || written >= required_size) {
        return {};
    }

    return std::wstring(buffer.data(), written);
}

[[nodiscard]] std::string CurrentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) %
        1000;
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
    if (localtime_s(&local_time, &now_time) != 0) {
        return "0000-00-00 00:00:00.000";
    }

    std::ostringstream output;
    output << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << '.'
           << std::setfill('0') << std::setw(3) << milliseconds.count();
    return output.str();
}

}  // namespace

bool Logger::Initialize(std::wstring* const error_message) {
    const std::wstring local_app_data =
        ReadEnvironmentVariable(L"LOCALAPPDATA");
    if (local_app_data.empty()) {
        if (error_message != nullptr) {
            *error_message = L"LOCALAPPDATA is not available.";
        }
        return false;
    }

    const std::filesystem::path log_directory =
        std::filesystem::path(local_app_data) / L"ChromeTaskbarMerger" /
        L"logs";

    std::error_code error;
    std::filesystem::create_directories(log_directory, error);
    if (error) {
        if (error_message != nullptr) {
            *error_message = L"Failed to create the log directory (error " +
                             std::to_wstring(error.value()) + L").";
        }
        return false;
    }

    log_path_ = log_directory / L"ChromeTaskbarMerger.log";
    stream_.open(log_path_, std::ios::out | std::ios::app | std::ios::binary);
    if (!stream_.is_open()) {
        if (error_message != nullptr) {
            *error_message = L"Failed to open the log file.";
        }
        log_path_.clear();
        return false;
    }

    return true;
}

void Logger::Info(const std::string_view message) {
    Write("INFO", message);
}

void Logger::Error(const std::string_view message) {
    Write("ERROR", message);
}

void Logger::Write(const std::string_view level,
                   const std::string_view message) {
    std::scoped_lock lock(mutex_);
    if (!stream_.is_open()) {
        return;
    }

    stream_ << CurrentTimestamp() << " [" << level << "] " << message << '\n';
    stream_.flush();
}

}  // namespace ctm
