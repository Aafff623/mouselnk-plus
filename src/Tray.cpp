#include "Tray.h"

namespace {
constexpr UINT kTrayIconId = 1;

// 图标与提示文本随暂停状态切换
constexpr const wchar_t* kTipRunning = L"mouselnk-plus — 已启用";
constexpr const wchar_t* kTipPaused  = L"mouselnk-plus — 已暂停";
}  // namespace

bool CTray::Add(HWND hwnd, HINSTANCE instance, bool paused) {
    m_iconNormal = static_cast<HICON>(
        ::LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                     ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    m_iconPaused = static_cast<HICON>(
        ::LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_PAUSED), IMAGE_ICON,
                     ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

    if (m_iconNormal == nullptr || m_iconPaused == nullptr) {
        return false;
    }

    m_nid = {};
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = hwnd;
    m_nid.uID = kTrayIconId;
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    m_nid.uCallbackMessage = WM_APP_TRAY;

    if (!Apply(paused)) {
        return false;
    }

    // 版本 4 让回调语义一致（鼠标消息在 lParam 低字）。
    NOTIFYICONDATAW version = m_nid;
    version.uVersion = NOTIFYICON_VERSION_4;
    ::Shell_NotifyIconW(NIM_SETVERSION, &version);

    m_added = true;
    return true;
}

bool CTray::SetPaused(bool paused) {
    if (!m_added) {
        return false;
    }
    return Apply(paused);
}

void CTray::Remove() {
    if (m_added) {
        ::Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_added = false;
    }
    if (m_iconNormal != nullptr) {
        ::DestroyIcon(m_iconNormal);
        m_iconNormal = nullptr;
    }
    if (m_iconPaused != nullptr) {
        ::DestroyIcon(m_iconPaused);
        m_iconPaused = nullptr;
    }
}

bool CTray::Apply(bool paused) {
    m_nid.hIcon = paused ? m_iconPaused : m_iconNormal;
    const wchar_t* tip = paused ? kTipPaused : kTipRunning;
    ::wcsncpy_s(m_nid.szTip, tip, _TRUNCATE);

    const DWORD op = m_added ? NIM_MODIFY : NIM_ADD;
    return ::Shell_NotifyIconW(op, &m_nid) != FALSE;
}

void CTray::NotifyAlreadyRunning() const {
    if (!m_added) {
        return;
    }
    NOTIFYICONDATAW nid = m_nid;
    nid.uFlags |= NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    ::wcsncpy_s(nid.szInfoTitle, L"mouselnk-plus", _TRUNCATE);
    ::wcsncpy_s(nid.szInfo, L"程序已在运行。", _TRUNCATE);
    ::Shell_NotifyIconW(NIM_MODIFY, &nid);
}

UINT CTray::PopupMenu(bool paused, bool autostartEnabled) {
    HMENU menu = ::CreatePopupMenu();
    if (menu == nullptr) {
        return 0;
    }

    ::AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"打开设置(&S)...");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING | (paused ? MF_CHECKED : MF_UNCHECKED),
                  IDM_TOGGLE_PAUSE, L"启用(&P)");
    ::AppendMenuW(menu, MF_STRING, IDM_RELOAD_CONFIG, L"重载配置(&C)");
    ::AppendMenuW(menu, MF_STRING | (autostartEnabled ? MF_CHECKED : MF_UNCHECKED),
                  IDM_AUTOSTART, L"开机启动(&A)");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"关于(&B)...");
    ::AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出(&X)");

    // 默认项：双击图标等同「打开设置」
    ::SetMenuDefaultItem(menu, IDM_SETTINGS, FALSE);

    POINT pt{};
    ::GetCursorPos(&pt);

    // 托盘菜单要求：弹出前把宿主窗口设为前台，否则点击别处菜单不会消失。
    HWND hwnd = m_nid.hWnd;
    ::SetForegroundWindow(hwnd);

    const UINT command = static_cast<UINT>(
        ::TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                         pt.x, pt.y, 0, hwnd, nullptr));

    // 官方推荐：菜单关闭后投一个空消息，避免菜单在极少数情况下残留。
    ::PostMessageW(hwnd, WM_NULL, 0, 0);
    ::DestroyMenu(menu);

    return command;
}
