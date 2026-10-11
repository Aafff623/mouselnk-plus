#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include <windows.h>

namespace input {

// 钩子存活看门狗（T-0033，调研结论 H-4 的落地）。
//
// 背景：WH_MOUSE_LL 回调执行超过 LowLevelHooksTimeout（默认 300ms）时，Windows 会
// **静默移除**该钩子且不通知任何人——HHOOK 仍非 NULL，只是再也不触发。这是「手势
// 软件莫名其妙失效、重启就好」的根因。系统不提供「钩子是否还在」的查询接口，唯一有
// 证据的判定方式是**独立投递路径对照**：
//   Raw Input 在动 + 钩子静默  => 钩子被丢弃
//   Raw Input 也静默          => 只是没人动鼠标
// 同一机制被 Aitiy 1.0.5 真实采用（反编译确认其参数：usUsagePage=1、usUsage=2、
// dwFlags=RIDEV_INPUTSINK、hwndTarget=自己的窗口，用于恢复被低级钩子漏掉的右键抬起）。
//
// 三路恢复信号（电源唤醒 / 显示状态变化 / 会话解锁）在 App 主窗口汇聚后转来：
// 睡眠唤醒时用户未必马上动鼠标，探针无从判定，只能主动重装——按 0.5s/3s/10s
// 阶梯重试、5s 去重（调研「现代待机后钩子失效」的结论）。会话锁定期间不判定：
// 锁屏输入不经过本会话的低级钩子，继续判定会把「锁屏」误报成「钩子被摘」。
//
// 线程模型：Start/Stop 与全部内部状态只能由钩子线程访问（窗口、计时器、Raw Input
// 都绑定该线程的消息队列）。NotifyResumeSignal / NotifySessionLocked 由 App 线程
// 调用，内部只 PostThreadMessage，不直接碰钩子线程的状态。
class Watchdog {
public:
    // 重装动作（卸旧钩子再装新钩子）。由钩子线程在计时器上下文里同步调用。
    using ReinstallFn = std::function<void()>;

    // 必须由钩子线程调用。reinstall 不能为空。
    bool Start(ReinstallFn reinstall);

    // 同样只能由钩子线程调用（随消息泵退出时调用）。
    void Stop();

    // 钩子回调里调用：只做一次 DWORD 写入。不是锁、不是分配、不是 I/O。
    void NoteHookEvent() {
        m_lastHookEventTick = ::GetTickCount();
        ++m_hookEventCount;
    }

    // 由 App 线程调用：恢复信号（电源/显示/解锁已汇聚）。
    void NotifyResumeSignal();

    // 由 App 线程调用：会话锁定状态（锁定期间暂停探针判定）。
    void NotifySessionLocked(bool locked);

    unsigned int reinstallCount() const { return m_reinstallCount.load(); }

    // 投递到钩子线程的自定义消息（PostThreadMessage 送达，hwnd 为空的线程消息）。
    // 定义在此处而不是 Input.cpp：消息循环在 Input.cpp，派发目标在本模块。
    // public 是刻意的——Input.cpp 的线程消息派发需要匹配这两个 ID。
    static constexpr UINT kThreadMsgResumeSignal = WM_APP + 12;
    static constexpr UINT kThreadMsgSessionLock = WM_APP + 13;

    // ---- 纯判定逻辑（无 Win32 依赖，进单元测试）----
    //
    // 「最近 kRawActiveWindowMs 内见过 Raw Input 活动」且「钩子已静默
    // kHookSilenceThresholdMs 以上」才判定钩子被移除。两者缺一不可：
    // 没有 Raw Input 活动时钩子静默是正常的（没人动鼠标）。
    static constexpr std::uint32_t kRawActiveWindowMs = 2000;
    static constexpr std::uint32_t kHookSilenceThresholdMs = 3000;

    static bool ShouldReinstallForSilence(std::uint32_t now, std::uint32_t lastHookEventTick,
                                          std::uint32_t lastRawInputTick) {
        const std::uint32_t rawSilentMs = now - lastRawInputTick;
        const std::uint32_t hookSilentMs = now - lastHookEventTick;
        return rawSilentMs <= kRawActiveWindowMs && hookSilentMs >= kHookSilenceThresholdMs;
    }

    // 钩子线程专用：会话锁定标志（锁定期间探针不判定）。
    void SetSessionLocked(bool locked) { m_sessionLocked = locked; }

    // 钩子线程专用：安排 0.5s/3s/10s 阶梯重装（5s 内重复调用去重；
    // 启动后 kStartupGraceMs 内忽略——见 .cpp 的说明）。
    void ArmRetryLadder();

    // 启动宽限期：RegisterPowerSettingNotification 在注册后会补发一条「当前
    // 显示状态」的 PBT_POWERSETTINGCHANGE（不是真实变化），每次启动都会触发
    // 一次；且钩子是刚装上的，此时重装没有意义。15s 内的恢复信号一律忽略
    // （探针不受影响，仍在跑）。
    static constexpr DWORD kStartupGraceMs = 15000;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 钩子线程专用
    void OnTimer();
    void AttemptReinstall(const char* reason);

    ReinstallFn m_reinstall;
    HWND        m_hwnd = nullptr;
    DWORD       m_threadId = 0;

    // 以下只由钩子线程读写（回调与计时器都在该线程），无需同步。
    DWORD m_startedTick = 0;
    DWORD m_lastHookEventTick = 0;
    DWORD m_lastRawInputTick = 0;
    DWORD m_lastResumeSignalTick = 0;
    DWORD m_lastReinstallAttemptTick = 0;
    DWORD m_lastHeartbeatTick = 0;
    // 累计计数（心跳日志用；DWORD 回绕无所谓，只看量级）
    unsigned long long m_hookEventCount = 0;
    unsigned long long m_rawInputCount = 0;
    // 阶梯重试的到期时间（GetTickCount 口径）；0 = 未安排。
    DWORD m_retryDueMs[3] = {0, 0, 0};
    bool  m_sessionLocked = false;

    // 跨线程读（About 对话框），写入在钩子线程。
    std::atomic<unsigned int> m_reinstallCount{0};
};

}  // namespace input
