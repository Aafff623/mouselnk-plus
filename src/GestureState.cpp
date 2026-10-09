#include "GestureState.h"

namespace gesture {

const char* StateName(State s) {
    switch (s) {
        case State::Idle:         return "Idle";
        case State::PendingStart: return "PendingStart";
        case State::Drawing:      return "Drawing";
        case State::WheelCombo:   return "WheelCombo";
        case State::Cancelled:    return "Cancelled";
        case State::Restoring:    return "Restoring";
    }
    return "?";
}

const char* KindName(Kind k) {
    switch (k) {
        case Kind::Trace:     return "Trace";
        case Kind::WheelUp:   return "WheelSwitchUp";
        case Kind::WheelDown: return "WheelSwitchDown";
        case Kind::None:      break;
    }
    return "None";
}

std::string FormatTrace(const Trace& t) {
    std::string line = "kind=";
    line += KindName(t.kind);
    line += " final=";
    line += StateName(t.finalState);
    line += " 点数=" + std::to_string(t.pointCount);
    line += " 时长ms=" + std::to_string(t.durationMs);
    line += " 起点=(" + std::to_string(t.start.x) + "," + std::to_string(t.start.y) + ")";
    line += " 终点=(" + std::to_string(t.end.x) + "," + std::to_string(t.end.y) + ")";
    line += " 状态迁移=[";
    for (int i = 0; i < t.transitionCount; ++i) {
        if (i > 0) {
            line += " -> ";
        }
        line += StateName(t.transitions[i].state);
        line += "@" + std::to_string(t.transitions[i].tick);
    }
    line += "]";
    return line;
}

namespace {
// 触发距离比较用平方，避免开方与浮点。
long long DistSq(Vec2 a, Vec2 b) {
    const long long dx = static_cast<long long>(a.x) - b.x;
    const long long dy = static_cast<long long>(a.y) - b.y;
    return dx * dx + dy * dy;
}
}  // namespace

void Machine::Enter(State s, std::uint32_t now) {
    m_state = s;
    Trace& t = m_slots[m_writeSlot];
    if (t.transitionCount < Trace::kMaxTransitions) {
        t.transitions[t.transitionCount++] = Transition{s, now};
    }
}

void Machine::AppendPoint(Vec2 p, std::uint32_t now) {
    Trace& t = m_slots[m_writeSlot];
    if (t.pointCount < Trace::kMaxPoints) {
        t.points[t.pointCount++] = TracePoint{p, now};
    }
    m_lastPoint = p;
    m_lastMoveTick = now;
}

void Machine::Reset() {
    m_state = State::Idle;
    m_wheelKind = Kind::None;
    m_reinjectRequested.store(false);
    m_readySlot.store(-1);
}

void Machine::Seal(Kind kind, State finalState, Vec2 end, std::uint32_t now) {
    Trace& t = m_slots[m_writeSlot];
    t.kind = kind;
    t.finalState = finalState;
    t.start = m_start;
    t.end = end;
    t.durationMs = now - m_startTick;

    // 已有一份未被取走的结果时，新的会覆盖它 —— 记为丢失，便于诊断宿主是否太慢。
    if (m_readySlot.load() >= 0) {
        m_lostResults.fetch_add(1);
    }
    m_readySlot.store(m_writeSlot);
    m_writeSlot ^= 1;
    // 下一份结果从干净状态开始
    Trace& next = m_slots[m_writeSlot];
    next.pointCount = 0;
    next.transitionCount = 0;
    next.kind = Kind::None;
    next.finalState = State::Idle;
}

bool Machine::OnRightDown(Vec2 p, std::uint32_t now) {
    // 已经在忙（例如上一次还没收尾）时不介入，把事件原样放行，避免打乱目标程序。
    if (m_state != State::Idle) {
        return false;
    }
    m_start = p;
    m_startTick = now;
    m_lastMoveTick = now;
    m_lastPoint = p;
    m_wheelKind = Kind::None;

    Trace& t = m_slots[m_writeSlot];
    t.pointCount = 0;
    t.transitionCount = 0;

    Enter(State::PendingStart, now);
    // 吞掉按下事件：此时还不知道是普通右键还是手势。
    // 若是普通右键，抬起时会补发一对 down/up，菜单照常出现（见 OnRightUp）。
    // 若不吞，目标程序会在用户画手势期间收到一个右键按下，被拖拽类操作干扰。
    return true;
}

bool Machine::OnMove(Vec2 p, std::uint32_t now) {
    switch (m_state) {
        case State::PendingStart: {
            const long long need = static_cast<long long>(m_settings.startDistance);
            if (DistSq(p, m_start) >= need * need) {
                Enter(State::Drawing, now);
                // 起点先入轨迹，避免轨迹缺头
                AppendPoint(m_start, now);
                AppendPoint(p, now);
            }
            break;   // 移动事件一律放行：目标程序本就收不到右键按下，不会被拖拽
        }
        case State::Drawing:
            AppendPoint(p, now);
            break;
        default:
            break;
    }
    return false;
}

bool Machine::OnRightUp(Vec2 p, std::uint32_t now) {
    switch (m_state) {
        case State::PendingStart:
            // 没达到触发距离 → 这是一次普通右键：补发一对 down/up 让菜单正常弹出。
            m_reinjectRequested.store(true);
            Enter(State::Restoring, now);
            return true;

        case State::Drawing:
            AppendPoint(p, now);
            Seal(Kind::Trace, State::Drawing, p, now);
            m_state = State::Idle;
            return true;

        case State::WheelCombo:
            // 规格要求：右键滚轮这次操作结束后不弹出右键菜单 → 吞掉抬起。
            Seal(m_wheelKind, State::WheelCombo, p, now);
            m_state = State::Idle;
            m_wheelKind = Kind::None;
            return true;

        default:
            return false;
    }
}

bool Machine::OnWheel(int delta, std::uint32_t now) {
    if (m_state != State::PendingStart && m_state != State::Drawing) {
        return false;   // 没按右键时的滚轮与我们无关
    }
    if (m_state == State::PendingStart) {
        // 还没到触发距离就滚轮：视为滚轮组合，不再当成绘制。
        Enter(State::WheelCombo, now);
    }
    m_wheelKind = delta > 0 ? Kind::WheelUp : Kind::WheelDown;
    m_lastMoveTick = now;
    // 吞掉滚轮，避免同时滚动目标程序内容
    return true;
}

bool Machine::CheckTimeout(std::uint32_t now) {
    if (m_state != State::Drawing) {
        return false;
    }
    const long long elapsed = static_cast<long long>(now) - m_lastMoveTick;
    if (elapsed <= m_settings.timeoutMs) {
        return false;
    }
    Enter(State::Cancelled, now);
    if (m_settings.restoreOnFailure) {
        // 规格：识别失败/超时是否恢复右键输入由配置决定
        m_reinjectRequested.store(true);
    }
    Seal(Kind::Trace, State::Cancelled, m_lastPoint, now);
    m_state = State::Idle;
    return true;
}

bool Machine::TakeReinjectRequest() {
    return m_reinjectRequested.exchange(false);
}

void Machine::NoteReinjected() {
    if (m_state == State::Restoring) {
        m_state = State::Idle;
    }
}

bool Machine::TakeCompleted(Trace* out) {
    const int slot = m_readySlot.exchange(-1);
    if (slot < 0 || out == nullptr) {
        return false;
    }
    *out = m_slots[slot];
    return true;
}

}  // namespace gesture
