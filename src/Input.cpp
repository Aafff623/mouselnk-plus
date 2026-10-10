#include "Input.h"

#include "Log.h"
#include "resource.h"

namespace input {
namespace {

// 投递到钩子线程的自定义消息（只能由 PostThreadMessage 送达，没有窗口）
constexpr UINT kThreadMsgPause      = WM_APP + 10;
constexpr UINT kThreadMsgReinjected = WM_APP + 11;

}  // namespace

Hook* Hook::s_instance = nullptr;

Hook& Hook::Instance() {
    static Hook instance;
    return instance;
}

bool Hook::Start(HWND notifyWindow) {
    if (m_running.load()) {
        return true;
    }
    m_notifyWindow = notifyWindow;
    s_instance = this;
    m_startState.store(0);

    m_thread = ::CreateThread(nullptr, 0, &Hook::ThreadEntry, this, 0, &m_threadId);
    if (m_thread == nullptr) {
        logger::Error("创建钩子线程失败，win32 错误 " + std::to_string(::GetLastError()));
        return false;
    }

    // 等钩子真正装好（或明确失败）再返回，避免调用方以为已经生效
    for (int i = 0; i < 300 && m_startState.load() == 0; ++i) {
        ::Sleep(10);
    }
    if (m_startState.load() != 1) {
        logger::Error("鼠标钩子安装失败（钩子线程报告未装好）");
        Stop();
        return false;
    }
    logger::Info("鼠标钩子已安装（专用消息泵线程 tid=" + std::to_string(m_threadId) + "）");
    return true;
}

void Hook::Stop() {
    if (m_thread == nullptr) {
        return;
    }
    // 让消息泵退出：WM_QUIT 使 GetMessageW 返回 0，线程随后在本线程内卸钩。
    if (m_threadId != 0) {
        ::PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
    }
    ::WaitForSingleObject(m_thread, 3000);
    ::CloseHandle(m_thread);
    m_thread = nullptr;
    m_threadId = 0;
    m_running.store(false);
    m_installed.store(false);
    logger::Info("鼠标钩子线程已退出");
}

DWORD WINAPI Hook::ThreadEntry(void* self) {
    return static_cast<Hook*>(self)->ThreadMain();
}

DWORD Hook::ThreadMain() {
    m_running.store(true);
    m_threadId = ::GetCurrentThreadId();

    // 必须在本线程装钩：低级钩子被投递到安装线程的消息队列。
    HHOOK hook = ::SetWindowsHookExW(WH_MOUSE_LL, &Hook::MouseProc, ::GetModuleHandleW(nullptr), 0);
    if (hook == nullptr) {
        logger::Error("SetWindowsHookEx(WH_MOUSE_LL) 失败，win32 错误 " +
                      std::to_string(::GetLastError()));
        m_running.store(false);
        m_startState.store(2);
        return 1;
    }
    m_installed.store(true);
    m_startState.store(1);

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.hwnd == nullptr) {
            // PostThreadMessage 送达的线程消息
            if (msg.message == kThreadMsgPause) {
                m_machine.Reset();
            } else if (msg.message == kThreadMsgReinjected) {
                m_machine.NoteReinjected();
            }
            continue;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    m_installed.store(false);
    // 卸载同样在本线程执行（与安装配对的唯一安全位置）
    ::UnhookWindowsHookEx(hook);
    m_running.store(false);
    return 0;
}

LRESULT CALLBACK Hook::MouseProc(int code, WPARAM wParam, LPARAM lParam) {
    Hook* self = s_instance;
    if (code < 0 || self == nullptr) {
        return ::CallNextHookEx(nullptr, code, wParam, lParam);
    }

    const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
    if (ms == nullptr) {
        return ::CallNextHookEx(nullptr, code, wParam, lParam);
    }

    // 自己补发的事件：直接放行，绝不再喂给状态机（否则递归）。
    if (ms->dwExtraInfo == kSelfInjectionTag) {
        return ::CallNextHookEx(nullptr, code, wParam, lParam);
    }

    // 暂停：全部放行（作者规格的暂停语义就是「不介入」）。
    if (self->m_paused.load()) {
        return ::CallNextHookEx(nullptr, code, wParam, lParam);
    }

    // 回调内只做：取坐标、喂状态机、按结果吞或放行。零分配、零锁、零 I/O。
    const gesture::Vec2 p{ms->pt.x, ms->pt.y};
    const std::uint32_t now = ::GetTickCount();

    bool consume = false;
    switch (wParam) {
        case WM_RBUTTONDOWN:
            consume = self->m_machine.OnRightDown(p, now);
            break;
        case WM_MOUSEMOVE:
            consume = self->m_machine.OnMove(p, now);
            break;
        case WM_RBUTTONUP:
            consume = self->m_machine.OnRightUp(p, now);
            break;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(ms->mouseData);
            consume = self->m_machine.OnWheel(delta, now);
            break;
        }
        default:
            break;
    }

    // 需要补发普通右键时，交给 App 线程执行 SendInput。
    // 不在回调里直接发：回调要尽量短（超 300ms 会被系统静默摘钩子）。
    if (self->m_machine.TakeReinjectRequest()) {
        ::PostMessageW(self->m_notifyWindow, WM_APP_REINJECT_CLICK,
                       static_cast<WPARAM>(static_cast<INT_PTR>(ms->pt.x)),
                       static_cast<LPARAM>(static_cast<INT_PTR>(ms->pt.y)));
    }

    // 轨迹实时投递给浮层：回调只做「喂状态机 + 投递」，绘制全部在 App 线程。
    // 进入绘制的那一跳先补起点，否则浮层的折线缺头。
    const gesture::State st = self->m_machine.state();
    if (self->m_prevState != gesture::State::Drawing && st == gesture::State::Drawing) {
        const gesture::Vec2 s = self->m_machine.startPoint();
        ::PostMessageW(self->m_notifyWindow, WM_APP_TRACE_POINT,
                       static_cast<WPARAM>(static_cast<INT_PTR>(s.x)),
                       static_cast<LPARAM>(static_cast<INT_PTR>(s.y)));
    }
    if (st == gesture::State::Drawing) {
        ::PostMessageW(self->m_notifyWindow, WM_APP_TRACE_POINT,
                       static_cast<WPARAM>(static_cast<INT_PTR>(p.x)),
                       static_cast<LPARAM>(static_cast<INT_PTR>(p.y)));
    }
    self->m_prevState = st;

    // 有封存好的结果就立刻通知宿主（否则要等 50ms 的定时器）。
    if (self->m_machine.hasReady()) {
        ::PostMessageW(self->m_notifyWindow, WM_APP_GESTURE_DONE, 0, 0);
    }

    if (consume) {
        return 1;   // 非 0 = 系统范围丢弃该事件
    }
    return ::CallNextHookEx(nullptr, code, wParam, lParam);
}

void Hook::SetPaused(bool paused) {
    m_paused.store(paused);
    if (paused && m_threadId != 0) {
        // 暂停时把状态机清干净，避免残留一个「画到一半」的手势
        ::PostThreadMessageW(m_threadId, kThreadMsgPause, 0, 0);
    }
}

void Hook::NotifyReinjected() {
    if (m_threadId != 0) {
        ::PostThreadMessageW(m_threadId, kThreadMsgReinjected, 0, 0);
    }
}

void Hook::ReinjectRightClick(POINT screenPoint) {
    (void)screenPoint;   // 不移动光标：用户释放时指针就在该位置，MoveCursor 会与用户抢夺控制权

    INPUT inputs[2]{};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
    inputs[0].mi.dwExtraInfo = kSelfInjectionTag;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    inputs[1].mi.dwExtraInfo = kSelfInjectionTag;

    const UINT sent = ::SendInput(2, inputs, sizeof(INPUT));
    m_reinjected.fetch_add(1);
    if (sent != 2) {
        logger::Warn("补发右键失败：SendInput 只送出 " + std::to_string(sent) + "/2（win32 错误 " +
                     std::to_string(::GetLastError()) + "）");
    } else {
        logger::Info("补发普通右键于 (" + std::to_string(screenPoint.x) + "," +
                     std::to_string(screenPoint.y) + ")（累计 " +
                     std::to_string(m_reinjected.load()) + " 次）");
    }
}

}  // namespace input
