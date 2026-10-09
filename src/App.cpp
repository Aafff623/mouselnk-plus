#include "App.h"

#include <cstdio>
#include <string>

#include "Config.h"
#include "resource.h"

namespace {

constexpr const wchar_t* kRunKeyPath   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValueName = L"mouselnk-plus";
constexpr const wchar_t* kAppTitle     = L"mouselnk-plus";
constexpr const wchar_t* kSingleInstanceMutex = L"Local\\mouselnk-plus.SingleInstance";
constexpr const wchar_t* kMainWindowClass     = L"mouselnk-plus.MainWindow";

// 崩溃兜底：进程异常退出时尽量摘掉托盘图标，避免留下「幽灵图标」。
// 这是一道最低限度的防线，不替代正规的退出清理路径。
NOTIFYICONDATAW g_crashTrayData{};
bool            g_crashTrayValid = false;
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;

LONG WINAPI CrashCleanupFilter(EXCEPTION_POINTERS* info) {
    if (g_crashTrayValid) {
        ::Shell_NotifyIconW(NIM_DELETE, &g_crashTrayData);
        g_crashTrayValid = false;
    }
    if (g_previousFilter != nullptr) {
        return g_previousFilter(info);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

std::wstring ExecutableDirectory() {
    return config::ExecutableDirectory().wstring();
}

std::wstring ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    if (need <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), need);
    return out;
}

// 最小启动诊断：把失败阶段与 win32 错误码落到 exe 同目录的日志里。
// 启动失败时 GUI 可能根本弹不出来（例如无交互桌面），没有这条日志就无从排查。
// 完整的 Logging 模块（分级、轮转、体积上限）是后续任务。
void LogStartupFailure(const wchar_t* stage, DWORD error) {
    const std::wstring dir = ExecutableDirectory();
    if (dir.empty()) {
        return;
    }
    const std::wstring file = dir + L"\\mouselnk-plus-startup.log";

    wchar_t line[512]{};
    ::swprintf_s(line, L"stage=%s win32Error=%lu timestamp=%llu\r\n", stage, error,
                 static_cast<unsigned long long>(::GetTickCount64()));

    HANDLE handle = ::CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
    if (bytes > 0) {
        std::string utf8(static_cast<size_t>(bytes - 1), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8.data(), bytes, nullptr, nullptr);
        DWORD written = 0;
        ::WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    ::CloseHandle(handle);
}

bool ReadAutostartValue() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    const LSTATUS status =
        ::RegQueryValueExW(key, kRunValueName, nullptr, &type, nullptr, &size);
    ::RegCloseKey(key);
    return status == ERROR_SUCCESS && type == REG_SZ;
}

bool WriteAutostartValue(bool enable) {
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                          &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    LSTATUS status = ERROR_SUCCESS;
    if (enable) {
        // 路径带引号，避免安装目录含空格时被拆成多个参数。
        const std::wstring command = L"\"" + config::ExecutablePath().wstring() + L"\"";
        status = ::RegSetValueExW(key, kRunValueName, 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(command.c_str()),
                                  static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = ::RegDeleteValueW(key, kRunValueName);
        if (status == ERROR_FILE_NOT_FOUND) {
            status = ERROR_SUCCESS;
        }
    }

    ::RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

void ReportPending(const wchar_t* feature, const wchar_t* task) {
    std::wstring text = feature;
    text += L"尚未实现。\n\n计划任务：";
    text += task;
    text += L"\n（见 TODO.md）";
    ::MessageBoxW(nullptr, text.c_str(), kAppTitle, MB_OK | MB_ICONINFORMATION);
}

}  // namespace

CApp& CApp::Instance() {
    static CApp instance;
    return instance;
}

CApp::StartResult CApp::Initialize(HINSTANCE instance) {
    m_instance = instance;

    // 单实例：Local\ 作用域 = 当前登录会话，同一用户不会起两个。
    ::SetLastError(ERROR_SUCCESS);
    m_singleInstance = ::CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
    if (m_singleInstance == nullptr) {
        LogStartupFailure(L"CreateMutex", ::GetLastError());
        return StartResult::Failed;
    }
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        // 已有实例：通知它（它会弹一个托盘提示），本实例直接退出。
        if (HWND existing = ::FindWindowW(kMainWindowClass, nullptr)) {
            ::PostMessageW(existing, WM_APP_SECOND_INSTANCE, 0, 0);
        }
        ::CloseHandle(m_singleInstance);
        m_singleInstance = nullptr;
        return StartResult::AlreadyRunning;
    }

    ::SetLastError(ERROR_SUCCESS);
    m_window.taskbarCreatedMessage = ::RegisterWindowMessageW(L"TaskbarCreated");
    if (m_window.taskbarCreatedMessage == 0) {
        // 不致命：只是收不到 Explorer 重启通知，托盘不会自动恢复。
        LogStartupFailure(L"RegisterWindowMessage(TaskbarCreated)", ::GetLastError());
    }

    ::SetLastError(ERROR_SUCCESS);
    if (m_window.Create(nullptr, CWindow::rcDefault, kAppTitle,
                        WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) == nullptr) {
        LogStartupFailure(L"CreateWindow", ::GetLastError());
        Shutdown();
        return StartResult::Failed;
    }

    ::SetLastError(ERROR_SUCCESS);
    if (!m_tray.Add(m_window.m_hWnd, m_instance, m_paused)) {
        LogStartupFailure(L"TrayAdd", ::GetLastError());
        Shutdown();
        return StartResult::Failed;
    }

    g_crashTrayData  = m_tray.Data();
    g_crashTrayValid = true;
    g_previousFilter = ::SetUnhandledExceptionFilter(&CrashCleanupFilter);

    // 配置：首次运行生成默认配置；损坏则备份原文件后回落默认。
    // 只有「损坏」与「失败」需要打扰用户，正常载入保持安静。
    const config::LoadResult loaded = config::Store::Instance().Load();
    m_configStatus = loaded.message;
    if (loaded.outcome == config::LoadOutcome::RecoveredCorrupt) {
        ::MessageBoxW(nullptr, ToWide(loaded.message).c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
    } else if (loaded.outcome == config::LoadOutcome::Failed) {
        LogStartupFailure(L"ConfigLoad", 0);
        ::MessageBoxW(nullptr, ToWide(loaded.message).c_str(), kAppTitle, MB_OK | MB_ICONERROR);
    }

    return StartResult::Ok;
}

int CApp::Run() {
    // 当前是空壳：主窗口隐藏，全部行为由托盘菜单驱动。
    // 阶段 1 的后续任务会在这里之前挂上钩子线程与手势状态机。
    CMessageLoop loop;
    _Module.AddMessageLoop(&loop);
    const int exitCode = loop.Run();
    _Module.RemoveMessageLoop();
    return exitCode;
}

void CApp::Shutdown() {
    g_crashTrayValid = false;
    m_tray.Remove();
    if (m_window.m_hWnd != nullptr) {
        m_window.DestroyWindow();
    }
    if (m_singleInstance != nullptr) {
        ::ReleaseMutex(m_singleInstance);
        ::CloseHandle(m_singleInstance);
        m_singleInstance = nullptr;
    }
}

void CApp::ExecuteCommand(UINT id) {
    switch (id) {
        case IDM_SETTINGS:      OpenSettings();    break;
        case IDM_TOGGLE_PAUSE:  TogglePause();     break;
        case IDM_RELOAD_CONFIG: ReloadConfig();    break;
        case IDM_AUTOSTART:     ToggleAutostart(); break;
        case IDM_ABOUT:         ShowAbout();       break;
        case IDM_EXIT:          ExitApp();         break;
        default:                break;
    }
}

void CApp::TogglePause() {
    m_paused = !m_paused;
    m_tray.SetPaused(m_paused);
}

void CApp::OpenSettings() {
    ReportPending(L"设置界面", L"T-0011 / T-0012（原生简单设置窗口，随后换 WebView2 页面）");
}

void CApp::ReloadConfig() {
    // 重载读入一份候选快照，验证通过才替换，因此失败时当前配置仍然有效。
    const config::LoadResult reloaded = config::Store::Instance().Reload();
    m_configStatus = reloaded.message;

    const config::Config current = config::Store::Instance().Snapshot();
    std::wstring text = ToWide(reloaded.message) + L"\n\n配置文件：" +
                        ToWide(config::Store::Instance().Path()) + L"\n模板数：" +
                        std::to_wstring(current.gestures.size()) + L"    全局绑定数：" +
                        std::to_wstring(current.matchGlobal.size()) + L"    应用规则数：" +
                        std::to_wstring(current.matchCustom.size());

    const bool ok = (reloaded.outcome == config::LoadOutcome::Loaded ||
                     reloaded.outcome == config::LoadOutcome::CreatedDefault);
    ::MessageBoxW(nullptr, text.c_str(), kAppTitle,
                  MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
}

void CApp::ToggleAutostart() {
    const bool enable = !ReadAutostartValue();
    if (!WriteAutostartValue(enable)) {
        ::MessageBoxW(nullptr, L"写入开机启动项失败。", kAppTitle, MB_OK | MB_ICONWARNING);
    }
}

void CApp::ShowAbout() {
    const config::Store& store = config::Store::Instance();
    const config::Config current = store.Snapshot();

    std::wstring text =
        std::wstring(kAppTitle) + L" 0.1.0\n\n"
        L"Windows 鼠标手势 / 鼠标增强工具。\n"
        L"以 MouseInc 为骨架独立复现，并计划集成部分 Aitiy 特性。\n\n"
        L"当前状态：阶段 1（托盘、单实例、暂停、开机启动、配置系统）。\n"
        L"图标为占位图，正式图标待定。\n\n"
        L"配置文件：" + ToWide(store.Path()) + L"\n"
        L"存储模式：" + (store.IsPortable() ? L"便携（exe 同目录）" : L"用户目录") + L"\n"
        L"SchemaVersion：" + std::to_wstring(current.schemaVersion) + L"\n\n"
        L"本程序不包含 MouseInc / Aitiy 的任何代码或资源。";
    ::MessageBoxW(nullptr, text.c_str(), kAppTitle, MB_OK | MB_ICONINFORMATION);
}

void CApp::ExitApp() {
    // 退出统一走这里：先摘托盘图标，再销毁窗口，最后退出消息循环。
    // 后续加入钩子与后台线程后，清理顺序在此扩展（见 CONTEXT.md 的 L-1/L-3）。
    g_crashTrayValid = false;
    m_tray.Remove();
    if (m_window.m_hWnd != nullptr) {
        m_window.DestroyWindow();
    }
}

// ---- CMainWindow ----

LRESULT CApp::CMainWindow::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    return 0;
}

LRESULT CApp::CMainWindow::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
    // 窗口是隐藏的，正常路径下不会收到 WM_CLOSE；收到也不销毁，
    // 退出只能由托盘菜单或系统注销触发。
    return 0;
}

LRESULT CApp::CMainWindow::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
    ::PostQuitMessage(0);
    return 0;
}

LRESULT CApp::CMainWindow::OnEndSession(UINT, WPARAM wParam, LPARAM, BOOL&) {
    if (wParam != FALSE) {
        CApp::Instance().Shutdown();
    }
    return 0;
}

LRESULT CApp::CMainWindow::OnTrayNotify(UINT, WPARAM, LPARAM lParam, BOOL&) {
    // NOTIFYICON_VERSION_4：通知消息在 lParam 低字；低字对旧版本同样成立。
    const UINT notification = LOWORD(lParam);
    if (notification == WM_RBUTTONUP || notification == WM_LBUTTONUP) {
        CApp& app = CApp::Instance();
        const UINT command = app.Tray().PopupMenu(app.IsPaused(), ReadAutostartValue());
        if (command != 0) {
            app.ExecuteCommand(command);
        }
    }
    return 0;
}

LRESULT CApp::CMainWindow::OnAppCommand(UINT, WPARAM wParam, LPARAM, BOOL&) {
    CApp::Instance().ExecuteCommand(static_cast<UINT>(wParam));
    return 0;
}

LRESULT CApp::CMainWindow::OnSecondInstance(UINT, WPARAM, LPARAM, BOOL&) {
    // 第二个实例已被拦截并退出，这里只做用户可感知的提示。
    CApp::Instance().Tray().NotifyAlreadyRunning();
    return 0;
}

LRESULT CApp::CMainWindow::OnTaskbarCreated(UINT, WPARAM, LPARAM, BOOL&) {
    // 消息映射用成员变量当消息 ID；若注册失败该值为 0，而 WM_NULL 也是 0，
    // 托盘菜单关闭后正好会 PostMessage(WM_NULL)，所以必须先挡住这种情况。
    if (taskbarCreatedMessage == 0) {
        return 0;
    }

    // Explorer 重启后托盘被清空，需要重新添加；暂停状态要一并恢复。
    CApp& app = CApp::Instance();
    app.Tray().Remove();
    app.Tray().Add(m_hWnd, app.InstanceHandle(), app.IsPaused());
    return 0;
}
