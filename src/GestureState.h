#pragma once

#include <atomic>
#include <cstdint>
#include <string>

// 手势状态机（作者规格「四、鼠标手势状态机」）。
//
// 它被设计成**纯逻辑**：不碰 Win32 输入、不分配内存、不抛异常，因此可以**直接跑在
// 钩子回调里**——这是必须的，因为「吞掉还是放行这个事件」必须当场决定，不能等另一个
// 线程。重活（识别、动作执行）由宿主线程在拿到完成的轨迹之后再做。
//
// 状态（规格要求的六态）：
//   Idle          空闲
//   PendingStart  右键按下但未达到触发距离
//   Drawing       正在绘制
//   WheelCombo    右键 + 滚轮组合
//   Cancelled     超时或取消
//   Restoring     正在补发普通右键（区分「补发点击」与「恢复拖动」）
namespace gesture {

struct Vec2 {
    int x = 0;
    int y = 0;
};

enum class State : std::uint32_t {
    Idle = 0,
    PendingStart,
    Drawing,
    WheelCombo,
    Cancelled,
    Restoring,
};

const char* StateName(State s);

// 一次手势的产出类型。WheelUp/WheelDown 是伪手势（不是画出来的形状）。
enum class Kind : std::uint32_t {
    None = 0,
    Trace,      // 画出来的轨迹
    WheelUp,    // 按住右键向上滚
    WheelDown,  // 按住右键向下滚
};

const char* KindName(Kind k);

struct Settings {
    int  startDistance = 5;      // 触发距离，单位：物理像素
    int  timeoutMs     = 1000;   // 移动停顿超时（不是整个手势的总时长）
    bool restoreOnFailure = false;
};

struct TracePoint {
    Vec2         p;
    std::uint32_t tick = 0;
};

struct Transition {
    State         state = State::Idle;
    std::uint32_t tick = 0;
};

struct Trace {
    static constexpr int kMaxPoints = 1024;
    static constexpr int kMaxTransitions = 24;

    Kind          kind = Kind::None;
    State         finalState = State::Idle;
    Vec2          start;
    Vec2          end;
    int           pointCount = 0;
    int           transitionCount = 0;
    std::uint32_t durationMs = 0;
    TracePoint    points[kMaxPoints];
    Transition    transitions[kMaxTransitions];
};

// 把一次结果格式化成一行（App 的日志与自检共用，避免两处格式漂移）。
std::string FormatTrace(const Trace& t);

class Machine {
public:
    void Configure(const Settings& s) { m_settings = s; }
    Settings CurrentSettings() const { return m_settings; }

    // ---- 钩子回调里调用：返回值 = 是否消费（消费即不交给 CallNextHookEx）----
    bool OnRightDown(Vec2 p, std::uint32_t now);
    bool OnMove(Vec2 p, std::uint32_t now);
    bool OnRightUp(Vec2 p, std::uint32_t now);
    bool OnWheel(int delta, std::uint32_t now);

    // ---- 宿主线程调用 ----
    // 移动停顿超时检测；返回是否因此发生了一次取消
    bool CheckTimeout(std::uint32_t now);

    // 是否需要补发一对普通右键（由 Win32 层执行 SendInput）
    bool TakeReinjectRequest();
    // 补发完成后通知状态机
    void NoteReinjected();

    // 取走最近一次完成的结果（一次性）。deltaTrace / lost 用于诊断。
    bool TakeCompleted(Trace* out);
    std::uint32_t lostResults() const { return m_lostResults; }

    void Reset();

    State state() const { return m_state; }
    bool  idle() const { return m_state == State::Idle; }
    Vec2  startPoint() const { return m_start; }
    bool  drawing() const { return m_state == State::Drawing; }

private:
    void Enter(State s, std::uint32_t now);
    void Seal(Kind kind, State finalState, Vec2 end, std::uint32_t now);

    Settings      m_settings{};
    State         m_state = State::Idle;
    Vec2          m_start{};
    Vec2          m_lastPoint{};      // 最近入轨迹的点（超时封存时作为终点）
    std::uint32_t m_startTick = 0;
    std::uint32_t m_lastMoveTick = 0;
    Kind          m_wheelKind = Kind::None;

    // 当前正在写入的缓冲区（只有钩子线程写）
    int      m_writeSlot = 0;
    Trace    m_slots[2]{};
    // 已封存、等待宿主取走的槽位（-1 表示无）
    std::atomic<int> m_readySlot{-1};
    std::atomic<bool> m_reinjectRequested{false};
    std::atomic<std::uint32_t> m_lostResults{0};

    void AppendPoint(Vec2 p, std::uint32_t now);
};

}  // namespace gesture
