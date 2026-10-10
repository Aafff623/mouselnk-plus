#pragma once

#include <windows.h>

#include "GestureState.h"

// 全局低级鼠标钩子（作者规格「一、3 输入监听」）。
//
// 三条硬约束（来自调研，见 docs/execution-plan.md 的 H 组）：
//   1. **钩子必须装在专用消息泵线程上**。低级钩子被投递到「安装它的那个线程」的消息
//      队列，该线程必须持续 pump；从别的线程装会装上一个永远收不到投递的钩子。
//      因此本模块自己起线程，且安装/卸载/重装一律通过 PostThreadMessage 回到该线程执行。
//   2. **回调内零分配、零锁、零 I/O、不回调出去**。回调超时会触发系统静默摘钩子
//      （LowLevelHooksTimeout，默认 300ms），所以状态机被做成纯逻辑 O(1) 写入。
//   3. **未消费的事件一律交给 CallNextHookEx**；返回非 0 是「在整个系统范围内丢弃该
//      事件」，不是只对本进程生效。
//
// 自身模拟输入用 dwExtraInfo 标记，避免补发的右键被自己的钩子再处理（递归）。
namespace input {

// 自身模拟输入的标记（ASCII 'MLNK'）。钩子见到带此标记的事件直接放行，避免把
// 自己注入的输入当成用户输入再处理一遍（补发右键、动作引擎的鼠标点击都用它）。
inline constexpr ULONG_PTR kSelfInjectionTag = 0x4D4C4E4B0001ull;

// 监听所有鼠标事件（不止右键）；未消费的一律放行。
class Hook {
public:
    static Hook& Instance();

    // 启动钩子线程并装钩。notifyWindow 用于接收 WM_APP_GESTURE_DONE。
    bool Start(HWND notifyWindow);

    // 卸钩并停线程（会等待线程结束，保证不留悬挂钩子）。
    void Stop();

    // 暂停时全部放行（作者规格「三、程序基础行为」的暂停语义）。
    void SetPaused(bool paused);
    bool paused() const { return m_paused.load(); }

    void SetSettings(const gesture::Settings& s) { m_machine.Configure(s); }
    gesture::Machine& Machine() { return m_machine; }

    bool installed() const { return m_installed.load(); }

    // 补发一对普通右键（带自身注入标记）。由 App 线程调用 —— 不在钩子回调里做 SendInput，
    // 因为回调必须尽量短（超 300ms 会被系统静默摘钩子）。
    void ReinjectRightClick(POINT screenPoint);

    // 补发完成后由 App 通知钩子线程，让状态机从 Restoring 回到 Idle。
    void NotifyReinjected();

    // 补发计数（诊断用：补发过多说明 StartDistance 或超时设置不合理）
    unsigned int reinjectedCount() const { return m_reinjected.load(); }

private:
    Hook() = default;
    Hook(const Hook&) = delete;
    Hook& operator=(const Hook&) = delete;

    static LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI ThreadEntry(void* self);

    DWORD ThreadMain();
    void  ApplyPendingPause();     // 只能由钩子线程调用

    HWND    m_notifyWindow = nullptr;
    HANDLE  m_thread = nullptr;
    DWORD   m_threadId = 0;

    std::atomic<bool>         m_installed{false};
    std::atomic<bool>         m_running{false};
    std::atomic<bool>         m_paused{false};
    std::atomic<unsigned int> m_reinjected{0};
    // 启动握手：0=进行中 1=已装好 2=安装失败。
    // 不要用 m_running 当作「线程已启动」的判据——它由钩子线程自己设置，
    // Start() 里的等待循环若依赖它，会在条件判断时就退出（曾经的真实 bug）。
    std::atomic<int>          m_startState{0};

    // 全局唯一实例的指针，供静态钩子过程访问（本程序单实例运行）
    static Hook* s_instance;

    // 钩子线程私有：上一次喂给状态机后的状态，用来检测「刚进入绘制」这一跳变
    // （进入绘制时要先把起点补给浮层，否则轨迹缺头）。只在钩子线程读写。
    gesture::State m_prevState = gesture::State::Idle;

    gesture::Machine m_machine;
};

}  // namespace input
