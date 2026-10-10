#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

// WTL 要求：atlapp.h 先于 atlwin.h，且 _Module 已声明。
#include <atlbase.h>
#include <atlapp.h>
extern CAppModule _Module;
#include <atlwin.h>

#include "Actions.h"
#include "Config.h"
#include "GestureState.h"
#include "Overlay.h"
#include "Recognizer.h"
#include "Tray.h"

// 应用级状态与命令分发。
//
// 窗口只负责搬运消息，行为集中在这里：托盘点击 → 菜单 → ExecuteCommand → 具体动作。
// 这样后续接入手势、热键、边缘滚动时，它们都只需调用 ExecuteCommand，复用同一条路径。
class CApp {
public:
    enum class StartResult {
        Ok,
        AlreadyRunning,   // 已有实例，本实例应立即退出
        Failed,
    };

    static CApp& Instance();

    // 单实例 + 主窗口 + 托盘，全部就绪后才返回 Ok。
    StartResult Initialize(HINSTANCE instance);

    // 跑消息循环，返回进程退出码。
    int Run();

    // 摘托盘图标、销毁窗口、释放单实例互斥体。可重复调用。
    void Shutdown();

    // 所有触发源（托盘菜单、后续的手势/热键/边缘/触发角）共用的命令入口。
    void ExecuteCommand(UINT id);

    bool IsPaused() const { return m_paused; }

    // 隐藏主窗口：承载托盘回调、TaskbarCreated 与模块间消息。
    // 注意不能用 message-only 窗口 —— 它收不到 TaskbarCreated 这类广播消息。
    class CMainWindow : public CWindowImpl<CMainWindow> {
    public:
        DECLARE_WND_CLASS_EX(L"mouselnk-plus.MainWindow", CS_HREDRAW | CS_VREDRAW, -1)

        BEGIN_MSG_MAP(CMainWindow)
            MESSAGE_HANDLER(WM_CREATE, OnCreate)
            MESSAGE_HANDLER(WM_CLOSE, OnClose)
            MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
            MESSAGE_HANDLER(WM_ENDSESSION, OnEndSession)
            MESSAGE_HANDLER(WM_TIMER, OnTimer)
            // TaskbarCreated 是运行时注册的消息，用成员变量作为消息 ID。
            // 注意 BEGIN_MSG_MAP 自身就会定义 ProcessWindowMessage，不要再自己声明。
            MESSAGE_HANDLER(taskbarCreatedMessage, OnTaskbarCreated)
            MESSAGE_HANDLER(WM_APP_TRAY, OnTrayNotify)
            MESSAGE_HANDLER(WM_APP_COMMAND, OnAppCommand)
            MESSAGE_HANDLER(WM_APP_SECOND_INSTANCE, OnSecondInstance)
            MESSAGE_HANDLER(WM_APP_GESTURE_DONE, OnGestureDone)
            MESSAGE_HANDLER(WM_APP_REINJECT_CLICK, OnReinjectClick)
            MESSAGE_HANDLER(WM_APP_TRACE_POINT, OnTracePointMsg)
            MESSAGE_HANDLER(WM_APP_SHOW_TIP, OnShowTipMsg)
        END_MSG_MAP()

        LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
        LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
        LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
        LRESULT OnEndSession(UINT, WPARAM, LPARAM, BOOL&);
        LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
        LRESULT OnGestureDone(UINT, WPARAM, LPARAM, BOOL&);
        LRESULT OnReinjectClick(UINT, WPARAM, LPARAM, BOOL&);
        LRESULT OnTracePointMsg(UINT, WPARAM, LPARAM, BOOL&);
        LRESULT OnShowTipMsg(UINT, WPARAM, LPARAM, BOOL&);
        LRESULT OnTrayNotify(UINT, WPARAM, LPARAM, BOOL&);
        LRESULT OnAppCommand(UINT, WPARAM, LPARAM, BOOL&);
        LRESULT OnSecondInstance(UINT, WPARAM, LPARAM, BOOL&);
        LRESULT OnTaskbarCreated(UINT, WPARAM, LPARAM, BOOL&);

        // RegisterWindowMessageW(L"TaskbarCreated")：Explorer 重启后会广播。
        UINT taskbarCreatedMessage = 0;
    };

    CMainWindow& Window() { return m_window; }
    CTray&       Tray() { return m_tray; }
    HINSTANCE    InstanceHandle() const { return m_instance; }

private:
    CApp() = default;
    CApp(const CApp&) = delete;
    CApp& operator=(const CApp&) = delete;

    void TogglePause();
    void OpenSettings();
    void ReloadConfig();
    void ToggleAutostart();
    void ShowAbout();
    void ExitApp();

    void ApplyGestureSettings();     // 把配置里的手势参数与模板同步给状态机与识别器
    void DrainGestureResults();      // 取走并处理已完成的手势结果
    void HandleGestureResult(const gesture::Trace& trace);   // 识别 → 查绑定 → 执行 → 反馈
    void OnTracePoint(int x, int y); // 浮层：追加一个轨迹点并重绘
    void ScheduleOverlayHide();      // 结果展示后延时收起浮层

    // 动作执行线程：动作链在后台串行执行，绝不阻塞钩子与消息循环。
    void StartActionWorker();
    void StopActionWorker();
    void EnqueueAction(const config::Binding& binding, const gesture::Trace& trace);

    HINSTANCE   m_instance = nullptr;
    HANDLE      m_singleInstance = nullptr;
    CMainWindow m_window;
    CTray       m_tray;
    bool        m_paused = false;
    std::string m_configStatus;   // 最近一次配置载入/重载的结果说明
    unsigned int m_lostGestureResults = 0;   // 已上报的「结果被覆盖」计数，避免重复告警

    gesture::Recognizer m_recognizer;

    // 当前手势的目标窗口（按下时记录，动作默认针对它）
    bool   m_gestureActive = false;
    HWND   m_gestureTarget = nullptr;
    std::wstring m_tempText;      // %tempstr% 的当前值（GetSelectedText 落地前为空）

    struct ActionJob {
        std::vector<config::Action> chain;
        actions::Context            ctx;
    };
    std::thread             m_actionThread;
    std::mutex              m_actionMutex;
    std::condition_variable m_actionCv;
    std::deque<ActionJob>   m_actionQueue;
    bool                    m_actionStop = false;
};
