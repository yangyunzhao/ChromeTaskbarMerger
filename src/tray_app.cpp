#include "tray_app.h"

#include "auto_start.h"
#include "chrome_window.h"
#include "ctm/version.h"
#include "fixed_entry_manager.h"
#include "logger.h"
#include "management_state.h"
#include "recovery_journal.h"
#include "single_instance.h"
#include "taskbar_controller.h"
#include "windowtabs_presence.h"

#include <Windows.h>
#include <CommCtrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ctm {
namespace {

constexpr UINT kTrayCallbackMessage = WM_APP + 10;
constexpr UINT_PTR kLifecycleTimerId = 1;
constexpr UINT kTrayIconId = 1;
constexpr int kApplicationIconResourceId = 101;

constexpr UINT kMenuStatus = 100;
constexpr UINT kMenuScanNow = 101;
constexpr UINT kMenuPause = 102;
constexpr UINT kMenuResume = 103;
constexpr UINT kMenuRestoreAll = 104;
constexpr UINT kMenuOpenLogs = 105;
constexpr UINT kMenuAbout = 106;
constexpr UINT kMenuStartWithWindows = 107;
constexpr UINT kMenuExit = 108;

constexpr wchar_t kProjectUrl[] =
    L"https://github.com/yangyunzhao/ChromeTaskbarMerger";

struct ManageableWindowScan {
    bool succeeded = false;
    DWORD error_code = ERROR_SUCCESS;
    std::vector<ChromeWindowSnapshot> windows;
};

[[nodiscard]] ManageableWindowScan ScanManageableWindows() {
    ManageableWindowScan scan;
    const ChromeWindowEnumerationResult enumeration =
        EnumerateChromeWindows();
    if (!enumeration.succeeded) {
        scan.error_code = enumeration.error_code;
        return scan;
    }
    for (const ChromeWindowRecord& record : enumeration.chrome_windows) {
        if (record.assessment.manageable) {
            scan.windows.push_back(record.snapshot);
        }
    }
    scan.succeeded = true;
    return scan;
}

[[nodiscard]] std::wstring FormatReport(
    const std::wstring_view label,
    const FixedEntryReport& report,
    const std::size_t tracked_removals) {
    std::wostringstream output;
    output << label << L": "
           << (report.succeeded ? L"SUCCESS" : L"FAIL")
           << L"; windows=" << report.manageable_window_count
           << L"; tracked=" << tracked_removals
           << L"; main_changed="
           << (report.main_entry_changed ? L"yes" : L"no")
           << L"; operations=" << report.operations.size()
           << L"; message=" << report.message;
    if (!report.persistence_error.empty()) {
        output << L"; persistence_error=" << report.persistence_error;
    }
    for (const FixedEntryOperation& operation : report.operations) {
        output << L"\n  "
               << (operation.kind == FixedEntryOperationKind::Remove
                       ? L"DeleteTab"
                       : L"AddTab")
               << L' ' << (operation.result.succeeded ? L"SUCCESS" : L"FAIL")
               << L"; hwnd=0x" << std::hex << std::uppercase
               << reinterpret_cast<std::uintptr_t>(operation.identity.hwnd)
               << std::dec << L"; pid=" << operation.identity.process_id
               << L"; created="
               << operation.identity.process_creation_time
               << L"; HRESULT=0x" << std::hex << std::uppercase
               << static_cast<std::uint32_t>(operation.result.hresult)
               << std::dec << L"; Win32="
               << operation.result.win32_error
               << L"; changed="
               << (operation.result.state_changed ? L"yes" : L"no")
               << L"; skipped="
               << (operation.result.skipped ? L"yes" : L"no")
               << L"; " << operation.result.message;
    }
    return output.str();
}

class TrayApplication final {
public:
    TrayApplication(HINSTANCE instance,
                    Logger* logger,
                    const AppConfig& config,
                    std::filesystem::path recovery_path,
                    std::filesystem::path configuration_path,
                    std::filesystem::path executable_path)
        : instance_(instance),
          logger_(logger),
          config_(config),
          configuration_path_(std::move(configuration_path)),
          executable_path_(std::move(executable_path)),
          recovery_journal_(std::move(recovery_path)),
          manager_(&controller_, &recovery_journal_) {}

    TrayApplication(const TrayApplication&) = delete;
    TrayApplication& operator=(const TrayApplication&) = delete;

    [[nodiscard]] int Run() {
        if (!CreateHiddenWindow()) {
            return 4;
        }
        if (!AddTrayIcon()) {
            LogError(L"Adding the notification-area icon failed.");
            DestroyWindow(window_);
            window_ = nullptr;
            return 4;
        }

        SynchronizeAutoStartRegistration();

        const TaskbarOperationResult initialization =
            controller_.InitializeTaskbarList();
        if (!initialization.succeeded) {
            management_state_.RequireRecovery();
            LogError(L"ITaskbarList initialization failed: " +
                     initialization.message);
            ShowNotification(
                L"需要恢复",
                L"任务栏接口初始化失败，请使用“恢复全部 Chrome 按钮”。",
                NIIF_ERROR);
        } else {
            InitializeRecoveryAndManagement();
        }

        static_cast<void>(RefreshLifecycle(L"startup initialization"));

        MSG message{};
        int exit_code = 0;
        while (true) {
            const BOOL result = GetMessageW(&message, nullptr, 0, 0);
            if (result == 0) {
                break;
            }
            if (result == -1) {
                LogError(L"GetMessageW failed with Win32 error " +
                         std::to_wstring(GetLastError()) + L'.');
                exit_code = 6;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (manager_.removed_window_count() != 0) {
            const bool restored = RestoreTracked(
                L"Message-loop exit restoration", false);
            if (!restored) {
                exit_code = 5;
            }
        }
        RemoveTrayIcon();
        controller_.Shutdown();
        if (window_ != nullptr && IsWindow(window_) != FALSE) {
            DestroyWindow(window_);
        }
        window_ = nullptr;
        UnregisterClassW(kTrayWindowClassName, instance_);
        return exit_code;
    }

private:
    [[nodiscard]] bool CreateHiddenWindow() {
        large_icon_ = static_cast<HICON>(LoadImageW(
            instance_,
            MAKEINTRESOURCEW(kApplicationIconResourceId),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXICON),
            GetSystemMetrics(SM_CYICON),
            LR_SHARED));
        small_icon_ = static_cast<HICON>(LoadImageW(
            instance_,
            MAKEINTRESOURCEW(kApplicationIconResourceId),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_SHARED));
        if (large_icon_ == nullptr || small_icon_ == nullptr) {
            LogError(L"Loading the embedded application icon failed; the "
                     L"Windows fallback icon will be used.");
        }

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &TrayApplication::WindowProcedure;
        window_class.hInstance = instance_;
        window_class.hIcon = large_icon_ != nullptr
                                 ? large_icon_
                                 : LoadIconW(nullptr, IDI_APPLICATION);
        window_class.hIconSm = small_icon_ != nullptr
                                   ? small_icon_
                                   : LoadIconW(nullptr, IDI_APPLICATION);
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = kTrayWindowClassName;
        if (RegisterClassExW(&window_class) == 0) {
            LogError(L"RegisterClassExW failed with Win32 error " +
                     std::to_wstring(GetLastError()) + L'.');
            return false;
        }

        taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
        if (taskbar_created_message_ == 0) {
            LogError(L"RegisterWindowMessageW(TaskbarCreated) failed with "
                     L"Win32 error " + std::to_wstring(GetLastError()) + L'.');
            UnregisterClassW(kTrayWindowClassName, instance_);
            return false;
        }
        window_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kTrayWindowClassName,
            L"ChromeTaskbarMerger",
            WS_OVERLAPPED,
            0,
            0,
            0,
            0,
            nullptr,
            nullptr,
            instance_,
            this);
        if (window_ == nullptr) {
            LogError(L"CreateWindowExW failed with Win32 error " +
                     std::to_wstring(GetLastError()) + L'.');
            UnregisterClassW(kTrayWindowClassName, instance_);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool AddTrayIcon() {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = kTrayIconId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = kTrayCallbackMessage;
        data.hIcon = small_icon_ != nullptr
                         ? small_icon_
                         : LoadIconW(nullptr, IDI_APPLICATION);
        const std::wstring tip = BuildTooltip();
        wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
        if (Shell_NotifyIconW(NIM_ADD, &data) == FALSE) {
            return false;
        }
        data.uVersion = NOTIFYICON_VERSION_4;
        static_cast<void>(Shell_NotifyIconW(NIM_SETVERSION, &data));
        tray_icon_added_ = true;
        return true;
    }

    void RemoveTrayIcon() noexcept {
        if (!tray_icon_added_ || window_ == nullptr) {
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = kTrayIconId;
        static_cast<void>(Shell_NotifyIconW(NIM_DELETE, &data));
        tray_icon_added_ = false;
    }

    void SynchronizeAutoStartRegistration() {
        const AutoStartOperationResult result =
            auto_start_registry_.SetEnabled(
                config_.start_with_windows, executable_path_);
        if (!result.succeeded) {
            LogError(
                L"Synchronizing Windows-login startup failed with Win32 "
                L"error " +
                std::to_wstring(result.error_code) + L'.');
            ShowNotification(
                L"自动启动配置未应用",
                L"无法同步 Windows 登录启动项，请查看日志。",
                NIIF_WARNING);
            return;
        }
        LogInfo(
            L"Windows-login startup synchronized; enabled=" +
            std::wstring(config_.start_with_windows ? L"true" : L"false") +
            L"; changed=" +
            std::wstring(result.changed ? L"yes" : L"no") + L'.');
    }

    void ToggleStartWithWindows() {
        const bool previous = config_.start_with_windows;
        const bool requested = !previous;
        const AppConfigSaveResult saved =
            SaveStartWithWindowsSetting(configuration_path_, requested);
        if (!saved.succeeded) {
            LogError(L"Saving start_with_windows failed: " +
                     saved.error_message);
            ShowNotification(
                L"自动启动设置未保存",
                L"无法写入 ChromeTaskbarMerger.ini，设置保持不变。",
                NIIF_ERROR);
            return;
        }

        const AutoStartOperationResult registered =
            auto_start_registry_.SetEnabled(requested, executable_path_);
        if (!registered.succeeded) {
            const AppConfigSaveResult rollback =
                SaveStartWithWindowsSetting(configuration_path_, previous);
            LogError(
                L"Updating Windows-login startup failed with Win32 error " +
                std::to_wstring(registered.error_code) +
                (rollback.succeeded
                     ? L"; the configuration change was rolled back."
                     : L"; configuration rollback also failed: " +
                           rollback.error_message));
            ShowNotification(
                L"自动启动设置未应用",
                rollback.succeeded
                    ? L"Windows 启动项更新失败，配置已恢复原值。"
                    : L"Windows 启动项和配置回滚均失败，请查看日志。",
                NIIF_ERROR);
            return;
        }

        config_.start_with_windows = requested;
        LogInfo(
            L"Windows-login startup changed from " +
            std::wstring(previous ? L"enabled" : L"disabled") + L" to " +
            std::wstring(requested ? L"enabled" : L"disabled") + L'.');
        ShowNotification(
            requested ? L"已启用自动启动" : L"已关闭自动启动",
            requested ? L"下次登录 Windows 时将自动运行。"
                      : L"下次登录 Windows 时不会自动运行。",
            NIIF_INFO);
    }

    void InitializeRecoveryAndManagement() {
        const RecoveryLoadResult load = recovery_journal_.Load();
        if (!load.succeeded) {
            management_state_.RequireRecovery();
            LogError(L"Recovery journal validation failed: " +
                     load.error_message);
            ShowNotification(
                L"需要人工恢复",
                L"恢复记录损坏或不可读。管理已暂停，请使用“恢复全部”。",
                NIIF_ERROR);
            return;
        }

        std::wstring adopt_error;
        if (!manager_.AdoptRecoveryStates(load.states, &adopt_error)) {
            management_state_.RequireRecovery();
            LogError(L"Adopting persisted recovery state failed: " +
                     adopt_error);
            return;
        }

        if (manager_.removed_window_count() != 0 &&
            !RestoreTracked(L"Startup persisted-state restoration", true)) {
            management_state_.RequireRecovery();
            ShowNotification(
                L"恢复未完成",
                L"上次会话的任务栏状态未能完全恢复，管理已暂停。",
                NIIF_ERROR);
            return;
        }

        const ProcessPresenceResult windowtabs = QueryWindowTabsPresence();
        const bool windowtabs_available =
            windowtabs.query_succeeded && windowtabs.running;
        management_state_.CompleteInitialization(windowtabs_available);
        if (!windowtabs_available) {
            if (windowtabs.query_succeeded) {
                LogInfo(
                    L"WindowTabs.exe is not running; continuous prerequisite "
                    L"waiting started.");
            } else {
                LogError(
                    L"WindowTabs detection failed during initialization with "
                    L"Win32 error " +
                    std::to_wstring(windowtabs.error_code) +
                    L"; prerequisite waiting will retry.");
            }
            ShowNotification(
                L"正在等待 WindowTabs",
                L"WindowTabs 就绪后将自动开始任务栏管理。",
                NIIF_INFO);
            return;
        }

        if (!Synchronize(L"Startup synchronization", true)) {
            return;
        }
        if (management_state_.managing()) {
            ShowNotification(
                L"ChromeTaskbarMerger",
                L"任务栏单入口管理已启动。",
                NIIF_INFO);
        }
    }

    [[nodiscard]] bool Synchronize(const std::wstring_view label,
                                   const bool notify_on_failure) {
        if (!management_state_.managing()) {
            return true;
        }

        const ProcessPresenceResult windowtabs = QueryWindowTabsPresence();
        if (!windowtabs.query_succeeded || !windowtabs.running) {
            const bool restored = RestoreTracked(
                L"WindowTabs prerequisite restoration", true);
            management_state_.WindowTabsBecameUnavailable(restored);
            const bool lifecycle_ready = RefreshLifecycle(
                L"WindowTabs prerequisite became unavailable");
            if (!restored) {
                LogError(
                    L"WindowTabs became unavailable and taskbar restoration "
                    L"did not complete; recovery is required.");
            } else if (!lifecycle_ready ||
                       !management_state_.waiting_for_windowtabs()) {
                LogError(
                    L"WindowTabs became unavailable, but continuous waiting "
                    L"could not be started.");
            } else {
                LogError(
                    windowtabs.query_succeeded
                        ? L"WindowTabs.exe is not running; management is "
                          L"waiting for it to restart."
                        : L"WindowTabs process detection failed with Win32 "
                          L"error " +
                              std::to_wstring(windowtabs.error_code) +
                              L"; management is waiting and will retry.");
            }
            if (notify_on_failure &&
                (management_state_.waiting_for_windowtabs() || !restored)) {
                ShowNotification(
                    restored ? L"正在等待 WindowTabs" : L"恢复未完成",
                    restored
                        ? L"WindowTabs 不可用；按钮已恢复，将在其重启后自动管理。"
                        : L"WindowTabs 不可用，且按钮未能完全恢复，请查看日志。",
                    restored ? NIIF_WARNING : NIIF_ERROR);
            }
            return false;
        }

        const ManageableWindowScan scan = ScanManageableWindows();
        if (!scan.succeeded) {
            LogError(L"Chrome window enumeration failed with Win32 error " +
                     std::to_wstring(scan.error_code) + L'.');
            const bool restored = RestoreTracked(
                L"Enumeration failure restoration", true);
            management_state_.OperationFailed(restored);
            static_cast<void>(RefreshLifecycle(
                L"Chrome window enumeration failure"));
            if (notify_on_failure) {
                ShowNotification(
                    restored ? L"管理异常暂停" : L"恢复未完成",
                    restored
                        ? L"Chrome 窗口扫描失败，可从托盘尝试恢复管理。"
                        : L"Chrome 窗口扫描失败，且按钮未能完全恢复。",
                    restored ? NIIF_WARNING : NIIF_ERROR);
            }
            return false;
        }

        const FixedEntryReport report = manager_.Synchronize(
            scan.windows, GetForegroundWindow());
        last_window_count_ = scan.windows.size();
        const bool significant = !report.succeeded ||
                                 report.main_entry_changed ||
                                 !report.operations.empty();
        if (significant) {
            LogInfo(FormatReport(
                label, report, manager_.removed_window_count()));
        }
        if (!report.succeeded) {
            const bool restored = RestoreTracked(
                L"Synchronization failure restoration", true);
            management_state_.OperationFailed(restored);
            static_cast<void>(RefreshLifecycle(
                L"taskbar synchronization failure"));
            if (notify_on_failure) {
                ShowNotification(
                    restored ? L"管理异常暂停" : L"恢复未完成",
                    restored
                        ? L"同步失败，按钮已恢复，可从托盘尝试恢复管理。"
                        : L"同步失败，且按钮未能完全恢复，请查看日志。",
                    restored ? NIIF_WARNING : NIIF_ERROR);
            }
        }
        UpdateTrayTooltip();
        return report.succeeded;
    }

    [[nodiscard]] bool RestoreTracked(const std::wstring_view label,
                                      const bool retry) {
        FixedEntryReport report = manager_.RestoreAll();
        LogInfo(FormatReport(label, report, manager_.removed_window_count()));
        if (report.succeeded && manager_.removed_window_count() == 0) {
            return true;
        }
        if (!retry) {
            return false;
        }
        report = manager_.RestoreAll();
        LogInfo(FormatReport(
            L"Restoration retry", report,
            manager_.removed_window_count()));
        return report.succeeded && manager_.removed_window_count() == 0;
    }

    [[nodiscard]] bool ForceRestoreAll() {
        StopLifecycleTimer();
        bool succeeded = RestoreTracked(
            L"Explicit tracked-state restoration", true);

        const ManageableWindowScan scan = ScanManageableWindows();
        if (!scan.succeeded) {
            LogError(L"Explicit restore enumeration failed with Win32 error " +
                     std::to_wstring(scan.error_code) + L'.');
            succeeded = false;
        } else {
            last_window_count_ = scan.windows.size();
            for (const ChromeWindowSnapshot& window : scan.windows) {
                const TaskbarOperationResult result =
                    controller_.ForceRestoreWindow(window);
                std::wostringstream message;
                message << L"Explicit AddTab "
                        << (result.succeeded ? L"SUCCESS" : L"FAIL")
                        << L"; hwnd=0x" << std::hex << std::uppercase
                        << reinterpret_cast<std::uintptr_t>(window.hwnd)
                        << std::dec << L"; HRESULT=0x" << std::hex
                        << static_cast<std::uint32_t>(result.hresult)
                        << std::dec << L"; Win32=" << result.win32_error
                        << L"; " << result.message;
                LogInfo(message.str());
                succeeded = succeeded && result.succeeded;
            }
        }

        if (manager_.removed_window_count() == 0) {
            std::wstring persistence_error;
            if (!manager_.ResetAfterTaskbarRecreation(&persistence_error)) {
                LogError(
                    L"Clearing recovery state after explicit restore failed: " +
                    persistence_error);
                succeeded = false;
            }
        } else {
            succeeded = false;
        }
        management_state_.ExplicitRestoreCompleted(succeeded);
        static_cast<void>(RefreshLifecycle(L"explicit restore-all completed"));
        ShowNotification(
            succeeded ? L"恢复完成" : L"恢复未完成",
            succeeded
                ? L"当前 Chrome 任务栏按钮已恢复，管理保持暂停。"
                : L"一个或多个按钮无法确认恢复，请查看日志。",
            succeeded ? NIIF_INFO : NIIF_ERROR);
        return succeeded;
    }

    void StopLifecycleTimer() noexcept {
        if (window_ != nullptr) {
            KillTimer(window_, kLifecycleTimerId);
        }
    }

    [[nodiscard]] bool UpdateLifecycleTimer() {
        if (window_ == nullptr) {
            return false;
        }
        StopLifecycleTimer();

        std::chrono::milliseconds interval{};
        std::wstring_view timer_name;
        if (management_state_.managing()) {
            interval = config_.scan_interval;
            timer_name = L"Chrome lifecycle scan";
        } else if (management_state_.waiting_for_windowtabs()) {
            interval = config_.windowtabs_check_interval;
            timer_name = L"WindowTabs prerequisite check";
        } else {
            return true;
        }

        SetLastError(ERROR_SUCCESS);
        if (SetTimer(
                window_, kLifecycleTimerId,
                static_cast<UINT>(interval.count()),
                nullptr) != 0) {
            return true;
        }

        const DWORD error_code = GetLastError();
        const bool was_managing = management_state_.managing();
        const bool restored =
            !was_managing ||
            RestoreTracked(L"Lifecycle-timer failure restoration", true);
        management_state_.OperationFailed(restored);
        LogError(
            L"Creating the " + std::wstring(timer_name) +
            L" timer failed with Win32 error " +
            std::to_wstring(error_code) + L"; state=" +
            std::wstring(ManagementStateLogName(management_state_.state())) +
            L'.');
        ShowNotification(
            restored ? L"监控已暂停" : L"恢复未完成",
            restored
                ? L"无法创建生命周期定时器，可从托盘尝试恢复管理。"
                : L"定时器创建失败，且按钮未能完全恢复，请查看日志。",
            restored ? NIIF_WARNING : NIIF_ERROR);
        return false;
    }

    [[nodiscard]] bool RefreshLifecycle(const std::wstring_view reason) {
        const bool timer_succeeded = UpdateLifecycleTimer();
        LogInfo(
            L"Management state applied: " +
            std::wstring(ManagementStateLogName(management_state_.state())) +
            L"; reason=" + std::wstring(reason) + L'.');
        UpdateTrayTooltip();
        return timer_succeeded;
    }

    void HandleWindowTabsCheck(const bool notify_if_still_waiting) {
        if (!management_state_.waiting_for_windowtabs()) {
            return;
        }

        const ProcessPresenceResult windowtabs = QueryWindowTabsPresence();
        if (!windowtabs.query_succeeded) {
            LogError(
                L"WindowTabs detection failed while waiting with "
                L"Win32 error " +
                std::to_wstring(windowtabs.error_code) + L'.');
            if (notify_if_still_waiting) {
                ShowNotification(
                    L"WindowTabs 检测失败",
                    L"程序将继续自动重试，请查看日志。",
                    NIIF_WARNING);
            }
            return;
        }
        if (!windowtabs.running) {
            if (notify_if_still_waiting) {
                ShowNotification(
                    L"仍在等待 WindowTabs",
                    L"检测到 WindowTabs 后将自动开始管理。",
                    NIIF_INFO);
            }
            return;
        }

        if (!management_state_.WindowTabsBecameAvailable()) {
            return;
        }
        if (!RefreshLifecycle(L"WindowTabs became available")) {
            return;
        }
        if (Synchronize(L"WindowTabs availability synchronization", true) &&
            management_state_.managing()) {
            ShowNotification(
                L"自动恢复管理",
                L"WindowTabs 已就绪，任务栏单入口管理已启动。",
                NIIF_INFO);
        }
    }

    void PauseManagement() {
        if (!management_state_.can_pause()) {
            return;
        }
        if (management_state_.waiting_for_windowtabs()) {
            management_state_.PauseByUser(true);
            static_cast<void>(RefreshLifecycle(
                L"user canceled WindowTabs waiting"));
            LogInfo(L"Continuous WindowTabs waiting was canceled by the user.");
            ShowNotification(
                L"管理已暂停",
                L"已取消等待 WindowTabs。",
                NIIF_INFO);
            return;
        }

        const bool restored = RestoreTracked(L"Tray pause restoration", true);
        management_state_.PauseByUser(restored);
        static_cast<void>(RefreshLifecycle(L"user requested pause"));
        ShowNotification(
            restored ? L"管理已暂停" : L"暂停恢复未完成",
            restored
                ? L"本程序移除的任务栏按钮已恢复。"
                : L"请查看日志并使用“恢复全部”。",
            restored ? NIIF_INFO : NIIF_ERROR);
    }

    void ResumeManagement() {
        if (management_state_.recovery_required()) {
            ShowNotification(
                L"需要先恢复",
                L"请先使用“恢复全部 Chrome 按钮”，再恢复管理。",
                NIIF_WARNING);
            return;
        }
        if (!management_state_.can_resume()) {
            return;
        }

        const ProcessPresenceResult windowtabs = QueryWindowTabsPresence();
        if (!windowtabs.query_succeeded) {
            LogError(
                L"WindowTabs detection failed during resume with Win32 error " +
                std::to_wstring(windowtabs.error_code) +
                L"; continuous waiting will retry.");
        }
        const bool available =
            windowtabs.query_succeeded && windowtabs.running;
        if (!management_state_.ResumeRequested(available)) {
            return;
        }
        if (!RefreshLifecycle(L"user requested resume")) {
            return;
        }
        if (management_state_.waiting_for_windowtabs()) {
            ShowNotification(
                L"正在等待 WindowTabs",
                L"WindowTabs 就绪后将自动恢复管理。",
                NIIF_INFO);
            return;
        }
        if (Synchronize(L"Tray resume synchronization", true) &&
            management_state_.managing()) {
            ShowNotification(
                L"管理已恢复",
                L"Chrome 任务栏单入口规则已重新应用。",
                NIIF_INFO);
        }
    }

    void HandleTaskbarCreated() {
        tray_icon_added_ = false;
        if (!AddTrayIcon()) {
            LogError(L"Re-adding the tray icon after TaskbarCreated failed.");
        }
        LogInfo(L"TaskbarCreated received; taskbar controller and tray icon "
                L"are being rebuilt.");

        controller_.Shutdown();
        std::wstring reset_error;
        if (!manager_.ResetAfterTaskbarRecreation(&reset_error)) {
            management_state_.RequireRecovery();
            LogError(L"Resetting recovery state after TaskbarCreated failed: " +
                     reset_error);
            static_cast<void>(RefreshLifecycle(
                L"taskbar recreation recovery reset failure"));
            return;
        }

        const TaskbarOperationResult initialization =
            controller_.InitializeTaskbarList();
        if (!initialization.succeeded) {
            management_state_.OperationFailed(true);
            LogError(L"Reinitializing ITaskbarList after TaskbarCreated failed: " +
                     initialization.message);
            static_cast<void>(RefreshLifecycle(
                L"taskbar recreation controller failure"));
            return;
        }
        if (management_state_.managing()) {
            static_cast<void>(Synchronize(
                L"TaskbarCreated synchronization", true));
        }
        static_cast<void>(RefreshLifecycle(L"taskbar recreation completed"));
    }

    void RequestExit() {
        StopLifecycleTimer();
        if (!RestoreTracked(L"Tray normal-exit restoration", true)) {
            management_state_.RequireRecovery();
            static_cast<void>(RefreshLifecycle(
                L"normal-exit restoration failure"));
            ShowNotification(
                L"暂不能退出",
                L"任务栏恢复未完成。请查看日志或使用“恢复全部”。",
                NIIF_ERROR);
            return;
        }
        RemoveTrayIcon();
        DestroyWindow(window_);
        window_ = nullptr;
    }

    static HRESULT CALLBACK AboutDialogCallback(
        const HWND dialog,
        const UINT notification,
        const WPARAM,
        const LPARAM parameter,
        const LONG_PTR callback_data) {
        if (notification != TDN_HYPERLINK_CLICKED || parameter == 0) {
            return S_OK;
        }

        const auto* const url = reinterpret_cast<const wchar_t*>(parameter);
        const HINSTANCE result = ShellExecuteW(
            dialog, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<std::intptr_t>(result) <= 32) {
            auto* const application = reinterpret_cast<TrayApplication*>(
                callback_data);
            if (application != nullptr) {
                application->LogError(
                    L"Opening the project URL from the About dialog failed.");
            }
            MessageBoxW(
                dialog,
                L"无法打开默认浏览器。\n\n"
                L"项目地址：\n"
                L"https://github.com/yangyunzhao/ChromeTaskbarMerger",
                L"ChromeTaskbarMerger",
                MB_OK | MB_ICONWARNING);
        }
        return S_OK;
    }

    void ShowAboutDialog() {
        const std::wstring instruction =
            L"ChromeTaskbarMerger " + std::wstring(kVersion);
        constexpr wchar_t content[] =
            L"Windows 任务栏 Chrome 单入口工具\n\n"
            L"开发人员：杨云召\n\n"
            L"GitHub：<a href=\"https://github.com/yangyunzhao/"
            L"ChromeTaskbarMerger\">https://github.com/yangyunzhao/"
            L"ChromeTaskbarMerger</a>";

        TASKDIALOGCONFIG configuration{};
        configuration.cbSize = sizeof(configuration);
        configuration.hwndParent = window_;
        configuration.hInstance = instance_;
        configuration.dwFlags =
            TDF_ENABLE_HYPERLINKS |
            TDF_ALLOW_DIALOG_CANCELLATION |
            TDF_POSITION_RELATIVE_TO_WINDOW |
            TDF_SIZE_TO_CONTENT;
        if (large_icon_ != nullptr) {
            configuration.dwFlags |= TDF_USE_HICON_MAIN;
            configuration.hMainIcon = large_icon_;
        }
        configuration.dwCommonButtons = TDCBF_OK_BUTTON;
        configuration.pszWindowTitle = L"关于 ChromeTaskbarMerger";
        configuration.pszMainInstruction = instruction.c_str();
        configuration.pszContent = content;
        configuration.pszFooter = L"许可证：MIT";
        configuration.pfCallback = &TrayApplication::AboutDialogCallback;
        configuration.lpCallbackData = reinterpret_cast<LONG_PTR>(this);

        using TaskDialogIndirectFunction = HRESULT(WINAPI*)(
            const TASKDIALOGCONFIG*, int*, int*, BOOL*);
        HRESULT result = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        const HMODULE common_controls = LoadLibraryW(L"comctl32.dll");
        if (common_controls != nullptr) {
            const auto task_dialog =
                reinterpret_cast<TaskDialogIndirectFunction>(GetProcAddress(
                    common_controls, "TaskDialogIndirect"));
            if (task_dialog != nullptr) {
                result = task_dialog(
                    &configuration, nullptr, nullptr, nullptr);
            }
            FreeLibrary(common_controls);
        }
        if (SUCCEEDED(result)) {
            return;
        }

        LogError(
            L"Showing the About task dialog failed with HRESULT " +
            std::to_wstring(static_cast<std::uint32_t>(result)) + L'.');
        const std::wstring fallback =
            instruction +
            L"\n\nWindows 任务栏 Chrome 单入口工具\n\n"
            L"开发人员：杨云召\n\n项目地址：\n" +
            kProjectUrl + L"\n\n许可证：MIT";
        MessageBoxW(
            window_,
            fallback.c_str(),
            L"关于 ChromeTaskbarMerger",
            MB_OK | MB_ICONINFORMATION);
    }

    [[nodiscard]] std::wstring_view ManagementStateDisplayName() const noexcept {
        switch (management_state_.state()) {
            case ManagementState::Initializing:
                return L"正在初始化";
            case ManagementState::WaitingForWindowTabs:
                return L"等待 WindowTabs";
            case ManagementState::Managing:
                return L"管理中";
            case ManagementState::PausedByUser:
                return L"已暂停（用户）";
            case ManagementState::PausedByError:
                return L"已暂停（异常）";
            case ManagementState::RecoveryRequired:
                return L"需要恢复";
        }
        return L"未知状态";
    }

    void ShowContextMenu(POINT point) {
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) {
            return;
        }
        const std::wstring status =
            L"状态：" + std::wstring(ManagementStateDisplayName());
        AppendMenuW(menu, MF_STRING | MF_DISABLED, kMenuStatus, status.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuScanNow, L"立即重新扫描");
        AppendMenuW(
            menu,
            MF_STRING |
                (management_state_.can_pause() ? MF_ENABLED : MF_GRAYED),
            kMenuPause,
            L"暂停管理");
        AppendMenuW(
            menu,
            MF_STRING |
                (management_state_.can_resume() ? MF_ENABLED : MF_GRAYED),
            kMenuResume,
            L"恢复管理");
        AppendMenuW(menu, MF_STRING, kMenuRestoreAll, L"恢复全部 Chrome 按钮");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(
            menu,
            MF_STRING |
                (config_.start_with_windows ? MF_CHECKED : MF_UNCHECKED),
            kMenuStartWithWindows,
            L"随 Windows 登录自动启动");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuOpenLogs, L"打开日志目录");
        AppendMenuW(menu, MF_STRING, kMenuAbout, L"关于 ChromeTaskbarMerger");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuExit, L"退出");

        SetForegroundWindow(window_);
        const UINT command = TrackPopupMenu(
            menu,
            TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
            point.x,
            point.y,
            0,
            window_,
            nullptr);
        DestroyMenu(menu);
        PostMessageW(window_, WM_NULL, 0, 0);
        HandleMenuCommand(command);
    }

    void HandleMenuCommand(const UINT command) {
        switch (command) {
            case kMenuScanNow:
                if (management_state_.managing()) {
                    static_cast<void>(Synchronize(
                        L"Tray manual synchronization", true));
                } else if (management_state_.waiting_for_windowtabs()) {
                    HandleWindowTabsCheck(true);
                } else {
                    const ManageableWindowScan scan = ScanManageableWindows();
                    if (scan.succeeded) {
                        last_window_count_ = scan.windows.size();
                        UpdateTrayTooltip();
                        ShowNotification(
                            L"只读扫描完成",
                            L"管理仍处于暂停状态。",
                            NIIF_INFO);
                    }
                }
                break;
            case kMenuPause:
                PauseManagement();
                break;
            case kMenuResume:
                ResumeManagement();
                break;
            case kMenuRestoreAll:
                static_cast<void>(ForceRestoreAll());
                break;
            case kMenuStartWithWindows:
                ToggleStartWithWindows();
                break;
            case kMenuOpenLogs:
                OpenLogDirectory();
                break;
            case kMenuAbout:
                ShowAboutDialog();
                break;
            case kMenuExit:
                RequestExit();
                break;
            default:
                break;
        }
    }

    void OpenLogDirectory() {
        if (logger_ == nullptr || logger_->log_directory().empty()) {
            ShowNotification(
                L"日志不可用",
                L"本次运行未能创建日志目录。",
                NIIF_WARNING);
            return;
        }
        const std::filesystem::path directory = logger_->log_directory();
        const HINSTANCE result = ShellExecuteW(
            window_, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<std::intptr_t>(result) <= 32) {
            LogError(L"Opening the log directory failed.");
        }
    }

    [[nodiscard]] std::wstring BuildTooltip() const {
        std::wstring tooltip = L"ChromeTaskbarMerger - " +
                               std::wstring(ManagementStateDisplayName());
        tooltip += L" (Chrome " + std::to_wstring(last_window_count_) + L")";
        return tooltip;
    }

    void UpdateTrayTooltip() {
        if (!tray_icon_added_ || window_ == nullptr) {
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = kTrayIconId;
        data.uFlags = NIF_TIP | NIF_SHOWTIP;
        const std::wstring tip = BuildTooltip();
        wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
        static_cast<void>(Shell_NotifyIconW(NIM_MODIFY, &data));
    }

    void ShowNotification(const std::wstring_view title,
                          const std::wstring_view text,
                          const DWORD flags) {
        if (!tray_icon_added_ || window_ == nullptr) {
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = kTrayIconId;
        data.uFlags = NIF_INFO;
        wcsncpy_s(
            data.szInfoTitle, std::wstring(title).c_str(), _TRUNCATE);
        wcsncpy_s(data.szInfo, std::wstring(text).c_str(), _TRUNCATE);
        data.dwInfoFlags = flags;
        static_cast<void>(Shell_NotifyIconW(NIM_MODIFY, &data));
    }

    void LogInfo(const std::wstring_view message) const {
        if (logger_ != nullptr) {
            logger_->Info(message);
        }
    }

    void LogError(const std::wstring_view message) const {
        if (logger_ != nullptr) {
            logger_->Error(message);
        }
    }

    LRESULT HandleMessage(const UINT message,
                          const WPARAM wparam,
                          const LPARAM lparam) {
        if (message == taskbar_created_message_) {
            HandleTaskbarCreated();
            return 0;
        }
        switch (message) {
            case WM_TIMER:
                if (wparam == kLifecycleTimerId) {
                    if (management_state_.managing()) {
                        static_cast<void>(Synchronize(
                            L"Periodic lifecycle synchronization", true));
                    } else if (
                        management_state_.waiting_for_windowtabs()) {
                        HandleWindowTabsCheck(false);
                    }
                }
                return 0;

            case kExternalCommandMessage:
                if (wparam == static_cast<WPARAM>(
                                  ExistingInstanceCommand::RestoreAll)) {
                    return ForceRestoreAll() ? 1 : 0;
                }
                if (wparam == static_cast<WPARAM>(
                                  ExistingInstanceCommand::Rescan)) {
                    if (management_state_.managing()) {
                        static_cast<void>(Synchronize(
                            L"Second-instance synchronization", true));
                    } else if (
                        management_state_.waiting_for_windowtabs()) {
                        HandleWindowTabsCheck(false);
                    } else {
                        const ManageableWindowScan scan =
                            ScanManageableWindows();
                        if (scan.succeeded) {
                            last_window_count_ = scan.windows.size();
                            LogInfo(
                                L"Second-instance read-only scan found " +
                                std::to_wstring(last_window_count_) +
                                L" manageable Chrome window(s); management "
                                L"remains paused.");
                            UpdateTrayTooltip();
                        }
                    }
                    ShowNotification(
                        L"ChromeTaskbarMerger",
                        L"已有实例正在运行，已请求重新扫描。",
                        NIIF_INFO);
                    return 1;
                }
                return 0;

            case kTrayCallbackMessage: {
                const UINT notification = LOWORD(lparam);
                if (notification == WM_CONTEXTMENU ||
                    notification == WM_RBUTTONUP ||
                    notification == NIN_SELECT ||
                    notification == NIN_KEYSELECT) {
                    POINT point{
                        GET_X_LPARAM(wparam),
                        GET_Y_LPARAM(wparam),
                    };
                    if (notification != WM_CONTEXTMENU ||
                        (point.x == -1 && point.y == -1)) {
                        GetCursorPos(&point);
                    }
                    ShowContextMenu(point);
                }
                return 0;
            }

            case WM_CLOSE:
                RequestExit();
                return 0;

            case WM_QUERYENDSESSION:
                static_cast<void>(RestoreTracked(
                    L"Windows session-end restoration", true));
                return TRUE;

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;

            default:
                return DefWindowProcW(window_, message, wparam, lparam);
        }
    }

    static LRESULT CALLBACK WindowProcedure(HWND window,
                                            const UINT message,
                                            const WPARAM wparam,
                                            const LPARAM lparam) {
        TrayApplication* application = nullptr;
        if (message == WM_NCCREATE) {
            const auto* const create =
                reinterpret_cast<const CREATESTRUCTW*>(lparam);
            application =
                static_cast<TrayApplication*>(create->lpCreateParams);
            application->window_ = window;
            SetWindowLongPtrW(
                window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(application));
        } else {
            application = reinterpret_cast<TrayApplication*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
        }
        return application != nullptr
                   ? application->HandleMessage(message, wparam, lparam)
                   : DefWindowProcW(window, message, wparam, lparam);
    }

    HINSTANCE instance_ = nullptr;
    Logger* logger_ = nullptr;
    AppConfig config_;
    std::filesystem::path configuration_path_;
    std::filesystem::path executable_path_;
    HWND window_ = nullptr;
    UINT taskbar_created_message_ = 0;
    HICON large_icon_ = nullptr;
    HICON small_icon_ = nullptr;
    bool tray_icon_added_ = false;
    std::size_t last_window_count_ = 0;
    AutoStartRegistry auto_start_registry_;
    ManagementStateMachine management_state_;
    TaskbarController controller_;
    RecoveryJournal recovery_journal_;
    FixedEntryManager manager_;
};

}  // namespace

bool SendTrayInstanceCommand(const HWND window,
                             const ExistingInstanceCommand command,
                             const bool wait_for_result,
                             DWORD* const error_code) {
    if (window == nullptr || IsWindow(window) == FALSE) {
        if (error_code != nullptr) {
            *error_code = ERROR_INVALID_WINDOW_HANDLE;
        }
        return false;
    }

    if (!wait_for_result) {
        const bool posted = PostMessageW(
                                window,
                                kExternalCommandMessage,
                                static_cast<WPARAM>(command),
                                0) != FALSE;
        if (error_code != nullptr) {
            *error_code = posted ? ERROR_SUCCESS : GetLastError();
        }
        return posted;
    }

    DWORD_PTR command_result = 0;
    SetLastError(ERROR_SUCCESS);
    const LRESULT sent = SendMessageTimeoutW(
        window,
        kExternalCommandMessage,
        static_cast<WPARAM>(command),
        0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        5000,
        &command_result);
    if (sent == 0 || command_result == 0) {
        if (error_code != nullptr) {
            const DWORD send_error = GetLastError();
            *error_code = send_error == ERROR_SUCCESS
                              ? ERROR_GEN_FAILURE
                              : send_error;
        }
        return false;
    }
    if (error_code != nullptr) {
        *error_code = ERROR_SUCCESS;
    }
    return true;
}

bool NotifyExistingTrayInstance(const ExistingInstanceCommand command,
                                const bool wait_for_result,
                                DWORD* const error_code) {
    HWND window = nullptr;
    for (int attempt = 0; attempt < 40 && window == nullptr; ++attempt) {
        window = FindWindowW(kTrayWindowClassName, nullptr);
        if (window == nullptr) {
            Sleep(50);
        }
    }
    if (window == nullptr) {
        if (error_code != nullptr) {
            *error_code = ERROR_FILE_NOT_FOUND;
        }
        return false;
    }
    return SendTrayInstanceCommand(
        window, command, wait_for_result, error_code);
}

int RunTrayApplication(
    const HINSTANCE instance,
    Logger* const logger,
    const AppConfig& config,
    const std::filesystem::path& recovery_journal_path,
    const std::filesystem::path& configuration_path,
    const std::filesystem::path& executable_path,
    const bool launched_at_login) {
    SingleInstanceGuard singleton;
    DWORD singleton_error = ERROR_SUCCESS;
    const SingleInstanceStatus status = singleton.Acquire(
        kSingletonName, &singleton_error);
    if (status == SingleInstanceStatus::Error) {
        if (logger != nullptr) {
            logger->Error(
                L"Creating the single-instance mutex failed with Win32 error " +
                std::to_wstring(singleton_error) + L'.');
        }
        return 4;
    }
    if (status == SingleInstanceStatus::Existing) {
        DWORD notification_error = ERROR_SUCCESS;
        const bool notified = NotifyExistingTrayInstance(
            ExistingInstanceCommand::Rescan, false, &notification_error);
        if (logger != nullptr) {
            if (notified) {
                logger->Info(
                    L"An existing tray instance was notified to rescan.");
            } else {
                logger->Error(
                    L"The existing instance could not be notified (Win32 " +
                    std::to_wstring(notification_error) + L").");
            }
        }
        return notified ? 0 : 4;
    }

    if (launched_at_login && !config.start_with_windows) {
        const AutoStartOperationResult cleanup =
            AutoStartRegistry().SetEnabled(false, executable_path);
        if (logger != nullptr) {
            if (cleanup.succeeded) {
                logger->Info(
                    L"A stale automatic-login invocation was ignored and "
                    L"its Run registration was removed.");
            } else {
                logger->Error(
                    L"Removing a stale automatic-login registration failed "
                    L"with Win32 error " +
                    std::to_wstring(cleanup.error_code) + L'.');
            }
        }
        return cleanup.succeeded ? 0 : 4;
    }

    TrayApplication application(
        instance,
        logger,
        config,
        recovery_journal_path,
        configuration_path,
        executable_path);
    return application.Run();
}

}  // namespace ctm
