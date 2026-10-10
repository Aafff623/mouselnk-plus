#include "App.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "Config.h"
#include "Input.h"
#include "Log.h"
#include "resource.h"

namespace {

// 手势超时检测的定时器：50ms 一次足够分辨 1s 量级的停顿超时，又不至于是忙等。
constexpr UINT_PTR kGestureTimerId = 1;
constexpr UINT     kGestureTimerMs = 50;

// 结果展示后的收起延时：让用户看清「成功 / 无匹配」的反馈，再自动消失。
constexpr UINT_PTR kOverlayHideTimerId = 2;
constexpr UINT     kOverlayHideMs = 700;

// "#RRGGBB" → COLORREF，解析失败时用 fallback。
COLORREF ParseColor(const std::string& text, COLORREF fallback) {
    if (text.size() == 7 && text[0] == '#') {
        const auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int r = hex(text[1]) * 16 + hex(text[2]);
        const int g = hex(text[3]) * 16 + hex(text[4]);
        const int b = hex(text[5]) * 16 + hex(text[6]);
        if (r >= 0 && g >= 0 && b >= 0) {
            return RGB(r, g, b);
        }
    }
    return fallback;
}

ui::OverlayStyle MakeOverlayStyle(const config::Config& cfg) {
    ui::OverlayStyle s;
    s.drawTrace  = cfg.gesture.drawTrace;
    s.drawResult = cfg.gesture.drawResult;
    s.traceArrow = cfg.gesture.traceArrow;
    s.randColor  = cfg.gesture.randColor;
    s.traceWidth = cfg.gesture.traceWidth;
    s.fontSize   = cfg.gesture.fontSize;
    s.drawColor  = ParseColor(cfg.gesture.drawColor, RGB(0xE4, 0x75, 0x42));
    s.failColor  = ParseColor(cfg.gesture.failColor, RGB(0xCA, 0xD0, 0xD3));
    return s;
}

// 取目标窗口的进程信息（动作上下文需要）。
void FillProcessInfo(HWND hwnd, actions::Context* ctx) {
    if (hwnd == nullptr) {
        return;
    }
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    ctx->pid = pid;
    if (pid == 0) {
        return;
    }
    HANDLE proc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc == nullptr) {
        return;
    }
    wchar_t buf[MAX_PATH]{};
    DWORD size = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
    if (::QueryFullProcessImageNameW(proc, 0, buf, &size)) {
        ctx->processPath = buf;
        const size_t slash = ctx->processPath.find_last_of(L"\\/");
        ctx->processName = (slash == std::wstring::npos) ? ctx->processPath
                                                         : ctx->processPath.substr(slash + 1);
    }
    ::CloseHandle(proc);
}

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
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    if (need <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), need);
    return out;
}

std::string ToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (need <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(need), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), need,
                          nullptr, nullptr);
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

    // 日志放在配置文件同目录（便携模式下即 exe 同目录），便于随程序搬走。
    const std::filesystem::path cfgPath = config::Store::Instance().Path();
    logger::Init(std::filesystem::path(cfgPath).parent_path() / L"mouselnk-plus.log");
    logger::Info("mouselnk-plus 启动；版本 " + std::string("0.1.0"));
    logger::Info("配置文件：" + config::Store::Instance().Path());
    logger::Info("存储模式：" + std::string(config::Store::Instance().IsPortable() ? "便携" : "用户目录"));

    // 轨迹浮层：初始化失败不致命，手势仍能识别与执行，只是看不到轨迹。
    if (!ui::Overlay::Instance().Initialize(m_instance)) {
        logger::Warn("轨迹浮层初始化失败：手势仍可用，但看不到轨迹");
    }

    // 动作执行线程：动作链在后台串行跑，绝不阻塞钩子与消息循环。
    StartActionWorker();

    // 手势：把配置同步给状态机，再装钩子。装钩子失败不致命（托盘仍可用），但必须让用户知道。
    ApplyGestureSettings();
    if (input::Hook::Instance().Start(m_window.m_hWnd)) {
        ::SetTimer(m_window.m_hWnd, kGestureTimerId, kGestureTimerMs, nullptr);
    } else {
        logger::Error("手势功能不可用：鼠标钩子安装失败");
        ::MessageBoxW(nullptr, L"鼠标钩子安装失败，手势功能不可用。\n详情见日志。", kAppTitle,
                      MB_OK | MB_ICONWARNING);
    }

    return StartResult::Ok;
}

void CApp::ApplyGestureSettings() {
    const config::Config cfg = config::Store::Instance().Snapshot();
    gesture::Settings s;
    s.startDistance = cfg.gesture.startDistance;
    s.timeoutMs = cfg.gesture.timeoutMs;
    s.restoreOnFailure = cfg.gesture.restoreOnFailure;
    input::Hook::Instance().SetSettings(s);

    // 识别器：模板与灵敏度都来自配置，重载后立即生效（无需重启）。
    std::vector<gesture::GestureTemplate> templates;
    templates.reserve(cfg.gestures.size());
    for (const config::GestureTemplate& g : cfg.gestures) {
        gesture::GestureTemplate t;
        t.id = g.id;
        t.points.reserve(g.points.size());
        for (const config::Point& p : g.points) {
            t.points.push_back(gesture::PointD{p.x, p.y});
        }
        templates.push_back(std::move(t));
    }
    m_recognizer.SetTemplates(std::move(templates));
    m_recognizer.SetSensitivity(cfg.gesture.sensitivity);

    logger::Info("手势参数：StartDistance=" + std::to_string(s.startDistance) +
                 " TimeoutMs=" + std::to_string(s.timeoutMs) +
                 " Sensitivity=" + std::to_string(cfg.gesture.sensitivity) +
                 " 模板数=" + std::to_string(m_recognizer.templateCount()) +
                 " RestoreOnFailure=" + std::string(s.restoreOnFailure ? "true" : "false"));
}

void CApp::DrainGestureResults() {
    gesture::Machine& machine = input::Hook::Instance().Machine();
    gesture::Trace trace;
    while (machine.TakeCompleted(&trace)) {
        logger::Info("手势结果 " + gesture::FormatTrace(trace));
        HandleGestureResult(trace);
    }
    const unsigned int lost = machine.lostResults();
    if (lost != m_lostGestureResults) {
        m_lostGestureResults = lost;
        logger::Warn("有手势结果因宿主线程未及时取走而被覆盖，累计 " + std::to_string(lost) + " 次");
    }
}

void CApp::OnTracePoint(int x, int y) {
    ui::Overlay& overlay = ui::Overlay::Instance();

    // 本次手势的第一个点：记录目标窗口并开始新的浮层轨迹。
    if (!m_gestureActive) {
        m_gestureActive = true;
        const POINT p{x, y};
        // 目标窗口取「手势起点所在的顶层窗口」，动作默认针对它（跨过别的窗口不改变目标）。
        m_gestureTarget = ::GetAncestor(::WindowFromPoint(p), GA_ROOT);
        overlay.BeginGesture();
    }

    overlay.AddPoint(POINT{x, y});
    overlay.DrawLive(MakeOverlayStyle(config::Store::Instance().Snapshot()));
}

void CApp::HandleGestureResult(const gesture::Trace& trace) {
    m_gestureActive = false;
    ::KillTimer(m_window.m_hWnd, kOverlayHideTimerId);

    const config::Config cfg = config::Store::Instance().Snapshot();
    const ui::OverlayStyle style = MakeOverlayStyle(cfg);
    ui::Overlay& overlay = ui::Overlay::Instance();

    if (trace.finalState == gesture::State::Cancelled) {
        overlay.Hide();
        return;
    }

    // 伪手势（右键滚轮）直接用固定标识，不走轨迹识别。
    std::string gestureId;
    if (trace.kind == gesture::Kind::WheelUp) {
        gestureId = "WheelSwitchUp";
    } else if (trace.kind == gesture::Kind::WheelDown) {
        gestureId = "WheelSwitchDown";
    } else if (trace.kind == gesture::Kind::Trace) {
        const gesture::MatchResult m =
            m_recognizer.Recognize(gesture::Recognizer::FromTrace(trace));
        if (m.matched) {
            gestureId = m.templateId;
            std::string top;
            for (size_t i = 0; i < m.candidates.size() && i < 3; ++i) {
                top += (i ? " " : "") + m.candidates[i].id + "=" +
                       std::to_string(static_cast<int>(m.candidates[i].score));
            }
            logger::Info("识别：" + gestureId + " 得分=" +
                         std::to_string(static_cast<int>(m.score)) + "（候选 " + top + "）");
        } else {
            logger::Info("识别：无匹配（阈值 " +
                         std::to_string(static_cast<int>(m_recognizer.threshold())) + "）");
        }
    }

    // 查绑定。应用规则（按程序匹配）是 T-0013 的范围，当前只查全局绑定。
    const config::Binding* binding = nullptr;
    for (const config::Binding& b : cfg.matchGlobal) {
        if (b.enabled && !gestureId.empty() && b.gestureId == gestureId) {
            binding = &b;
            break;
        }
    }

    if (binding == nullptr) {
        const bool matchedSomething = !gestureId.empty();
        overlay.ShowResult(style,
                           matchedSomething ? ui::Overlay::ResultState::NoAction
                                            : ui::Overlay::ResultState::NoMatch,
                           L"");
        ScheduleOverlayHide();
        return;
    }

    overlay.ShowResult(style, ui::Overlay::ResultState::Success, ToWide(binding->name));
    ScheduleOverlayHide();
    EnqueueAction(*binding, trace);
}

void CApp::ScheduleOverlayHide() {
    ::SetTimer(m_window.m_hWnd, kOverlayHideTimerId, kOverlayHideMs, nullptr);
}

void CApp::StartActionWorker() {
    m_actionStop = false;
    m_actionThread = std::thread([this]() {
        for (;;) {
            ActionJob job;
            {
                std::unique_lock<std::mutex> lock(m_actionMutex);
                m_actionCv.wait(lock, [this]() { return m_actionStop || !m_actionQueue.empty(); });
                if (m_actionStop && m_actionQueue.empty()) {
                    return;
                }
                job = std::move(m_actionQueue.front());
                m_actionQueue.pop_front();
            }
            // 动作链在工作线程串行执行：Delay / SendKeys / 启动程序都不会卡住 UI 与钩子。
            const int failed = actions::ExecuteChain(job.chain, job.ctx, m_window.m_hWnd);
            if (failed < 0) {
                logger::Info("动作链执行完成（" + std::to_string(job.chain.size()) + " 个动作）");
            }
        }
    });
}

void CApp::StopActionWorker() {
    {
        std::lock_guard<std::mutex> lock(m_actionMutex);
        m_actionStop = true;
        m_actionQueue.clear();
    }
    m_actionCv.notify_all();
    if (m_actionThread.joinable()) {
        m_actionThread.join();
    }
}

void CApp::EnqueueAction(const config::Binding& binding, const gesture::Trace& trace) {
    ActionJob job;
    job.chain = binding.actions;   // 拷贝一份：配置可能在执行期间被重载
    job.ctx.start = POINT{trace.start.x, trace.start.y};
    job.ctx.end = POINT{trace.end.x, trace.end.y};
    job.ctx.targetWindow = m_gestureTarget;
    job.ctx.tempText = m_tempText;
    FillProcessInfo(m_gestureTarget, &job.ctx);

    // 轨迹包围矩形
    if (trace.pointCount > 0) {
        LONG l = trace.points[0].p.x, r = l, t = trace.points[0].p.y, b = t;
        for (int i = 1; i < trace.pointCount; ++i) {
            l = std::min<LONG>(l, trace.points[i].p.x);
            r = std::max<LONG>(r, trace.points[i].p.x);
            t = std::min<LONG>(t, trace.points[i].p.y);
            b = std::max<LONG>(b, trace.points[i].p.y);
        }
        job.ctx.bounds = RECT{l, t, r, b};
    }

    logger::Info("执行动作：" + binding.name + "（" + binding.gestureId + "，" +
                 std::to_string(job.chain.size()) + " 个动作，目标 " +
                 (job.ctx.processName.empty() ? std::string("未知")
                                              : ToUtf8(job.ctx.processName)) +
                 "）");

    {
        std::lock_guard<std::mutex> lock(m_actionMutex);
        m_actionQueue.push_back(std::move(job));
    }
    m_actionCv.notify_one();
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
    if (m_window.m_hWnd != nullptr) {
        ::KillTimer(m_window.m_hWnd, kGestureTimerId);
        ::KillTimer(m_window.m_hWnd, kOverlayHideTimerId);
    }
    // 退出顺序不可颠倒（调研 L-1）：先卸钩子并停钩子线程，再停动作线程、收浮层，
    // 然后销毁窗口、摘托盘图标，最后关日志。反过来的话，钩子回调或动作链可能在
    // 窗口对象已销毁之后触发。
    input::Hook::Instance().Stop();
    logger::Info("已卸载鼠标钩子，准备退出");
    StopActionWorker();
    ui::Overlay::Instance().Shutdown();
    m_tray.Remove();
    if (m_window.m_hWnd != nullptr) {
        m_window.DestroyWindow();
    }
    if (m_singleInstance != nullptr) {
        ::ReleaseMutex(m_singleInstance);
        ::CloseHandle(m_singleInstance);
        m_singleInstance = nullptr;
    }
    logger::Shutdown();
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
    // 暂停 = 钩子全部放行，且清掉可能画到一半的手势
    input::Hook::Instance().SetPaused(m_paused);
    logger::Info(std::string("已") + (m_paused ? "暂停" : "恢复"));
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

    // 重载后把新手势参数同步给状态机（否则要重启才生效）
    ApplyGestureSettings();
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
        L"当前状态：阶段 1（托盘、单实例、暂停、开机启动、配置系统、鼠标钩子与手势状态机）。\n"
        L"识别与动作执行尚未接入。\n\n"
        L"配置文件：" + ToWide(store.Path()) + L"\n"
        L"存储模式：" + (store.IsPortable() ? L"便携（exe 同目录）" : L"用户目录") + L"\n"
        L"SchemaVersion：" + std::to_wstring(current.schemaVersion) + L"\n"
        L"鼠标钩子：" +
        std::wstring(input::Hook::Instance().installed() ? L"已安装" : L"未安装") +
        (input::Hook::Instance().paused() ? L"（已暂停）" : L"") + L"\n"
        L"日志：" + ToWide(logger::Path()) + L"\n\n"
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

LRESULT CApp::CMainWindow::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL&) {
    if (wParam == kGestureTimerId) {
        CApp& app = CApp::Instance();
        // 停顿超时（注意：是「移动停顿」超时，不是整个手势的总时长）
        input::Hook::Instance().Machine().CheckTimeout(::GetTickCount());
        app.DrainGestureResults();
    } else if (wParam == kOverlayHideTimerId) {
        ::KillTimer(m_hWnd, kOverlayHideTimerId);
        ui::Overlay::Instance().Hide();
    }
    return 0;
}

LRESULT CApp::CMainWindow::OnTracePointMsg(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    // 钩子线程投递的轨迹点：只做「追加 + 重绘」，不做识别（识别等轨迹封存后一次算）。
    // 这里先把队列里积压的同类消息一次性抽干，避免高频移动时每个点都重绘一次。
    MSG msg{};
    int x = static_cast<int>(static_cast<INT_PTR>(wParam));
    int y = static_cast<int>(static_cast<INT_PTR>(lParam));
    while (::PeekMessageW(&msg, m_hWnd, WM_APP_TRACE_POINT, WM_APP_TRACE_POINT, PM_REMOVE)) {
        x = static_cast<int>(static_cast<INT_PTR>(msg.wParam));
        y = static_cast<int>(static_cast<INT_PTR>(msg.lParam));
    }
    CApp::Instance().OnTracePoint(x, y);
    return 0;
}

LRESULT CApp::CMainWindow::OnShowTipMsg(UINT, WPARAM wParam, LPARAM, BOOL&) {
    // 动作线程把提示文本的所有权交过来，这里负责显示并释放。
    std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(wParam));
    if (text != nullptr) {
        CApp::Instance().Tray().ShowBalloon(L"mouselnk-plus", *text);
    }
    return 0;
}

LRESULT CApp::CMainWindow::OnGestureDone(UINT, WPARAM, LPARAM, BOOL&) {
    CApp::Instance().DrainGestureResults();
    return 0;
}

LRESULT CApp::CMainWindow::OnReinjectClick(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    // 钩子线程把「这次是普通右键」的结论交过来，由本线程执行注入。
    // 放在这里而不是回调里：钩子回调必须尽量短，SendInput 不是回调该做的事。
    POINT pt{static_cast<LONG>(static_cast<INT_PTR>(wParam)),
             static_cast<LONG>(static_cast<INT_PTR>(lParam))};
    input::Hook& hook = input::Hook::Instance();
    hook.ReinjectRightClick(pt);
    hook.NotifyReinjected();
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
