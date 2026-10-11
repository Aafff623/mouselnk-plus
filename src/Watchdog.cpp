#include "Watchdog.h"

#include <string>

#include "Log.h"

namespace input {
namespace {

constexpr UINT     kWatchdogTimerId = 1;
constexpr UINT_PTR kWatchdogTimerPeriodMs = 1000;              // 判定粒度 1s
constexpr DWORD    kResumeDedupMs = 5000;                      // 恢复信号 5s 去重
constexpr DWORD    kProbeDedupMs = 5000;                       // 探针判死后两次重装的最小间隔
constexpr DWORD    kRetryDelaysMs[3] = {500, 3000, 10000};     // 阶梯重试：0.5s/3s/10s
constexpr DWORD    kHeartbeatMs = 300000;                      // 状态心跳 5 分钟（诊断用）
constexpr wchar_t  kWindowClassName[] = L"mouselnk-plus.HookWatchdog";

// GetTickCount 环绕安全的「now 是否已到 due」（间隔远小于 49.7 天时成立）。
bool TimeReached(DWORD now, DWORD due) {
    return static_cast<std::int32_t>(now - due) >= 0;
}

}  // namespace

bool Watchdog::Start(ReinstallFn reinstall) {
    m_reinstall = std::move(reinstall);
    m_threadId = ::GetCurrentThreadId();

    // 窗口类由本模块注册；已注册过（重复 Start）时 RegisterClassW 失败也无妨。
    WNDCLASSW wc{};
    wc.lpfnWndProc = &Watchdog::WindowProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClassName;
    ::RegisterClassW(&wc);

    // message-only 窗口：收 Raw Input（RIDEV_INPUTSINK 后台也投递）与计时器。
    // 不可见、不进任务栏、不抢焦点；注意不能用它收 TaskbarCreated 之类的广播
    // （那是主窗口的职责），这里只收定向投递的 WM_INPUT / WM_TIMER。
    m_hwnd = ::CreateWindowExW(0, kWindowClassName, nullptr, 0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, wc.hInstance, this);
    if (m_hwnd == nullptr) {
        logger::Warn("看门狗窗口创建失败，钩子存活看门狗不可用（win32 错误 " +
                     std::to_string(::GetLastError()) + "）");
        return false;
    }

    // Raw Input 旁路探针。绝不用 RIDEV_NOLEGACY——它会彻底关掉该设备类的传统
    // 鼠标消息（WM_MOUSEMOVE / WM_LBUTTONDOWN 全部消失），官方标注对标准应用
    // 「exceptionally dangerous」。RIDEV_INPUTSINK 只是保证后台也投递。
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;              // HID generic desktop
    rid.usUsage = 0x02;                  // mouse
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = m_hwnd;
    if (!::RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        logger::Warn("RegisterRawInputDevices 失败，钩子存活看门狗不可用（win32 错误 " +
                     std::to_string(::GetLastError()) + "）");
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    // 两个基准必须同时起步：否则启动后用户一动鼠标就会出现「Raw Input 活跃 +
    // 钩子静默 3 秒以上」的假阳性（钩子事件计数还是 0）。
    // 心跳基准也要起步：GetTickCount 是系统 uptime，不初始化的话开机超过
    // 5 分钟的机器上一启动就会立刻打一条心跳。
    const DWORD now = ::GetTickCount();
    m_startedTick = now;
    m_lastHookEventTick = now;
    m_lastRawInputTick = now;
    m_lastHeartbeatTick = now;

    ::SetTimer(m_hwnd, kWatchdogTimerId, kWatchdogTimerPeriodMs, nullptr);
    logger::Info("钩子存活看门狗已启用（Raw Input 探针阈值 " +
                 std::to_string(kHookSilenceThresholdMs) + "ms，恢复信号阶梯 0.5s/3s/10s，1s 周期）");
    return true;
}

void Watchdog::Stop() {
    if (m_hwnd != nullptr) {
        ::KillTimer(m_hwnd, kWatchdogTimerId);
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage = 0x02;
        rid.dwFlags = RIDEV_REMOVE;
        rid.hwndTarget = m_hwnd;
        ::RegisterRawInputDevices(&rid, 1, sizeof(rid));
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    ::UnregisterClassW(kWindowClassName, ::GetModuleHandleW(nullptr));
    m_reinstall = nullptr;
}

void Watchdog::NotifyResumeSignal() {
    if (m_threadId != 0) {
        ::PostThreadMessageW(m_threadId, kThreadMsgResumeSignal, 0, 0);
    }
}

void Watchdog::NotifySessionLocked(bool locked) {
    if (m_threadId != 0) {
        ::PostThreadMessageW(m_threadId, kThreadMsgSessionLock, locked ? 1 : 0, 0);
    }
}

void Watchdog::ArmRetryLadder() {
    const DWORD now = ::GetTickCount();
    // 启动宽限期：RegisterPowerSettingNotification 注册后会补发一条当前显示
    // 状态的 PBT_POWERSETTINGCHANGE（实测每次启动都会来），不区分真实变化；
    // 且钩子是刚装上的。此时安排重装没有意义，忽略即可（探针不受影响）。
    if (now - m_startedTick < kStartupGraceMs) {
        logger::Info("看门狗：启动宽限期内忽略恢复信号（钩子刚安装，且注册电源通知会补发当前状态）");
        return;
    }
    // 5s 去重：睡眠唤醒瞬间三路信号可能同时到达，重复调用不重启阶梯。
    if (now - m_lastResumeSignalTick < kResumeDedupMs) {
        return;
    }
    m_lastResumeSignalTick = now;
    for (int i = 0; i < 3; ++i) {
        m_retryDueMs[i] = now + kRetryDelaysMs[i];   // 0.5s / 3s / 10s
    }
    logger::Info("看门狗：收到恢复信号，安排 0.5s/3s/10s 阶梯重装");
}

LRESULT CALLBACK Watchdog::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Watchdog* self = nullptr;
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<Watchdog*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Watchdog*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self == nullptr) {
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
        case WM_INPUT:
            // 事件到达即「独立投递路径活着」的证据。不需要读数据——
            // 这里只要时间戳，GetRawInputData 读不读都不影响投递。
            self->m_lastRawInputTick = ::GetTickCount();
            ++self->m_rawInputCount;
            return 0;

        case WM_TIMER:
            if (wParam == kWatchdogTimerId) {
                self->OnTimer();
            }
            return 0;

        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Watchdog::OnTimer() {
    const DWORD now = ::GetTickCount();

    // 0) 状态心跳：让用户（和我们）能从日志确认两条投递路径都活着。
    //    钩子事件数在涨 = 钩子活着；Raw Input 数在涨 = 探针活着。
    if (now - m_lastHeartbeatTick >= kHeartbeatMs) {
        m_lastHeartbeatTick = now;
        logger::Info("看门狗状态：钩子事件 " + std::to_string(m_hookEventCount) +
                     " / Raw Input " + std::to_string(m_rawInputCount) +
                     " / 重装 " + std::to_string(m_reinstallCount.load()) + " 次" +
                     (m_sessionLocked ? "（会话已锁定，探针暂停）" : ""));
    }

    // 1) 恢复信号安排的阶梯重装。与探针无关：唤醒时用户可能还没动鼠标，
    //    那时无从判定钩子死活，只能无条件重装（卸下再装上的间隙是亚毫秒级）。
    for (DWORD& due : m_retryDueMs) {
        if (due != 0 && TimeReached(now, due)) {
            due = 0;
            AttemptReinstall("恢复信号");
        }
    }

    // 2) 常驻探针：Raw Input 在动 + 钩子静默 => 钩子被系统静默移除。
    //    会话锁定期间不判定：锁屏上的鼠标活动不经过本会话的低级钩子，
    //    继续判定会把「已锁定」误报成「钩子被摘」。
    //    m_lastReinstallAttemptTick 初值 0，因此开机 5s 内探针不动作——
    //    启动初期两个基准同时起步，本来也不该判定。
    if (!m_sessionLocked &&
        ShouldReinstallForSilence(now, m_lastHookEventTick, m_lastRawInputTick) &&
        TimeReached(now, m_lastReinstallAttemptTick + kProbeDedupMs)) {
        AttemptReinstall("探针");
    }
}

void Watchdog::AttemptReinstall(const char* reason) {
    m_lastReinstallAttemptTick = ::GetTickCount();
    if (!m_reinstall) {
        return;
    }
    logger::Warn(std::string("看门狗：钩子疑似被系统移除（原因 ") + reason + "），执行第 " +
                 std::to_string(m_reinstallCount.load() + 1) + " 次重装");
    m_reinstall();
    m_reinstallCount.fetch_add(1);
}

}  // namespace input
