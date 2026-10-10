#include "Actions.h"

#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Input.h"     // kSelfInjectionTag
#include "Log.h"
#include "resource.h"

namespace actions {
namespace {

// ---- 小工具 ----

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (need <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), need);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) {
        return {};
    }
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (need <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(need), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), need,
                          nullptr, nullptr);
    return out;
}

std::wstring Upper(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(::towupper(c)); });
    return s;
}

std::wstring Trim(const std::wstring& s) {
    const size_t b = s.find_first_not_of(L" \t");
    if (b == std::wstring::npos) {
        return {};
    }
    const size_t e = s.find_last_not_of(L" \t");
    return s.substr(b, e - b + 1);
}

bool HasKey(const config::json& j, const char* k) {
    return j.is_object() && j.contains(k);
}

std::wstring ParamString(const config::json& j, const char* k, const wchar_t* def = L"") {
    if (!HasKey(j, k)) {
        return def;
    }
    const config::json& v = j[k];
    return v.is_string() ? Utf8ToWide(v.get<std::string>()) : std::wstring(def);
}

int ParamInt(const config::json& j, const char* k, int def) {
    if (!HasKey(j, k)) {
        return def;
    }
    const config::json& v = j[k];
    return v.is_number_integer() ? v.get<int>() : def;
}

bool ParamBool(const config::json& j, const char* k, bool def) {
    if (!HasKey(j, k)) {
        return def;
    }
    const config::json& v = j[k];
    return v.is_boolean() ? v.get<bool>() : def;
}

// ---- 按键名表 ----

struct KeyEntry {
    const wchar_t* name;
    WORD           vk;
    bool           extended;
};

const KeyEntry kKeys[] = {
    {L"BACK", VK_BROWSER_BACK, false},
    {L"FORWARD", VK_BROWSER_FORWARD, false},
    {L"BACKSPACE", VK_BACK, false},
    {L"BKSP", VK_BACK, false},
    {L"TAB", VK_TAB, false},
    {L"ENTER", VK_RETURN, false},
    {L"RETURN", VK_RETURN, false},
    {L"ESC", VK_ESCAPE, false},
    {L"ESCAPE", VK_ESCAPE, false},
    {L"SPACE", VK_SPACE, false},
    {L"PGUP", VK_PRIOR, true},
    {L"PAGEUP", VK_PRIOR, true},
    {L"PGDN", VK_NEXT, true},
    {L"PAGEDOWN", VK_NEXT, true},
    {L"HOME", VK_HOME, true},
    {L"END", VK_END, true},
    {L"DEL", VK_DELETE, true},
    {L"DELETE", VK_DELETE, true},
    {L"INS", VK_INSERT, true},
    {L"INSERT", VK_INSERT, true},
    {L"UP", VK_UP, true},
    {L"DOWN", VK_DOWN, true},
    {L"LEFT", VK_LEFT, true},
    {L"RIGHT", VK_RIGHT, true},
    {L"PRINTSCREEN", VK_SNAPSHOT, true},
    {L"CAPSLOCK", VK_CAPITAL, false},
    {L"NUMLOCK", VK_NUMLOCK, true},
    {L"SCROLLLOCK", VK_SCROLL, false},
    {L"VOLUMEUP", VK_VOLUME_UP, true},
    {L"VOLUMEDOWN", VK_VOLUME_DOWN, true},
    {L"VOLUMEMUTE", VK_VOLUME_MUTE, true},
    {L"MEDIANEXT", VK_MEDIA_NEXT_TRACK, true},
    {L"MEDIAPREV", VK_MEDIA_PREV_TRACK, true},
    {L"MEDIAPLAY", VK_MEDIA_PLAY_PAUSE, true},
    {L"MEDIASTOP", VK_MEDIA_STOP, true},
};

// ---- 执行原语 ----

bool SendInputs(const std::vector<INPUT>& inputs, std::wstring* err) {
    if (inputs.empty()) {
        return true;
    }
    const UINT sent = ::SendInput(static_cast<UINT>(inputs.size()),
                                  const_cast<INPUT*>(inputs.data()), sizeof(INPUT));
    if (sent != inputs.size()) {
        *err = L"SendInput 只送出 " + std::to_wstring(sent) + L"/" +
               std::to_wstring(inputs.size()) + L" 个事件（win32 错误 " +
               std::to_wstring(::GetLastError()) + L"）";
        return false;
    }
    return true;
}

// "Ctrl+Shift+S" / "Back" / "F5" → 一组键盘事件
bool SendKeysSpec(const std::wstring& spec, std::wstring* err) {
    std::vector<std::wstring> parts;
    std::wstring cur;
    for (const wchar_t c : spec) {
        if (c == L'+') {
            parts.push_back(Trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    parts.push_back(Trim(cur));

    if (parts.empty() || parts.back().empty()) {
        *err = L"SendKeys 缺少按键";
        return false;
    }

    WORD vk = 0;
    bool extended = false;
    if (!ResolveVirtualKey(parts.back(), &vk, &extended)) {
        *err = L"无法识别的按键：" + parts.back();
        return false;
    }

    std::vector<WORD> mods;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::wstring m = Upper(parts[i]);
        if (m == L"CTRL" || m == L"CONTROL") {
            mods.push_back(VK_CONTROL);
        } else if (m == L"ALT") {
            mods.push_back(VK_MENU);
        } else if (m == L"SHIFT") {
            mods.push_back(VK_SHIFT);
        } else if (m == L"WIN" || m == L"WINDOWS") {
            mods.push_back(VK_LWIN);
        } else {
            *err = L"无法识别的修饰键：" + parts[i];
            return false;
        }
    }

    std::vector<INPUT> inputs;
    for (const WORD m : mods) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = m;
        inputs.push_back(in);
    }
    INPUT kd{};
    kd.type = INPUT_KEYBOARD;
    kd.ki.wVk = vk;
    kd.ki.dwFlags = extended ? KEYEVENTF_EXTENDEDKEY : 0;
    inputs.push_back(kd);
    INPUT ku = kd;
    ku.ki.dwFlags |= KEYEVENTF_KEYUP;
    inputs.push_back(ku);
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = *it;
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(in);
    }
    return SendInputs(inputs, err);
}

bool SendKeyState(const std::wstring& spec, bool down, std::wstring* err) {
    WORD vk = 0;
    bool extended = false;
    if (!ResolveVirtualKey(spec, &vk, &extended)) {
        *err = L"无法识别的按键：" + spec;
        return false;
    }
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = (extended ? KEYEVENTF_EXTENDEDKEY : 0) | (down ? 0 : KEYEVENTF_KEYUP);
    return SendInputs({in}, err);
}

bool SendMouseClick(const std::wstring& button, std::wstring* err) {
    const std::wstring b = Upper(button);
    DWORD downFlag = 0;
    DWORD upFlag = 0;
    DWORD data = 0;
    if (b == L"LEFT") {
        downFlag = MOUSEEVENTF_LEFTDOWN;
        upFlag = MOUSEEVENTF_LEFTUP;
    } else if (b == L"RIGHT") {
        downFlag = MOUSEEVENTF_RIGHTDOWN;
        upFlag = MOUSEEVENTF_RIGHTUP;
    } else if (b == L"MIDDLE") {
        downFlag = MOUSEEVENTF_MIDDLEDOWN;
        upFlag = MOUSEEVENTF_MIDDLEUP;
    } else if (b == L"X1" || b == L"X2") {
        downFlag = MOUSEEVENTF_XDOWN;
        upFlag = MOUSEEVENTF_XUP;
        data = (b == L"X1") ? XBUTTON1 : XBUTTON2;
    } else {
        *err = L"未知鼠标按键：" + button;
        return false;
    }

    std::vector<INPUT> inputs(2);
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = downFlag;
    inputs[0].mi.mouseData = data;
    inputs[0].mi.dwExtraInfo = input::kSelfInjectionTag;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = upFlag;
    inputs[1].mi.mouseData = data;
    inputs[1].mi.dwExtraInfo = input::kSelfInjectionTag;
    return SendInputs(inputs, err);
}

// 目标窗口是否仍然有效（规格：使用前验证目标窗口仍有效）。
bool TargetAlive(HWND hwnd) {
    return hwnd != nullptr && ::IsWindow(hwnd) != FALSE;
}

bool DoWindowCommand(HWND hwnd, const std::wstring& command, std::wstring* err) {
    if (!TargetAlive(hwnd)) {
        *err = L"目标窗口已失效";
        return false;
    }
    const std::wstring c = Upper(command);

    if (c == L"CLOSE") {
        ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return true;
    }
    if (c == L"MINIMIZE") {
        ::ShowWindow(hwnd, SW_MINIMIZE);
        return true;
    }
    if (c == L"MAXIMIZE") {
        ::ShowWindow(hwnd, SW_MAXIMIZE);
        return true;
    }
    if (c == L"RESTORE") {
        ::ShowWindow(hwnd, SW_RESTORE);
        return true;
    }
    if (c == L"TOGGLEMAXIMIZED") {
        ::ShowWindow(hwnd, ::IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        return true;
    }
    if (c == L"HIDETRAY") {
        ::ShowWindow(hwnd, SW_HIDE);
        return true;
    }
    if (c == L"SHOWTRAY" || c == L"RESTOREFROMTRAY") {
        ::ShowWindow(hwnd, SW_SHOW);
        return true;
    }
    if (c == L"TOPT" || c == L"TOP" || c == L"TOGGLEMOSTTOP") {
        const bool wasTop = (::GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        ::SetWindowPos(hwnd, wasTop ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return true;
    }
    if (c == L"CENTER") {
        RECT rc{};
        if (!::GetWindowRect(hwnd, &rc)) {
            *err = L"取窗口矩形失败";
            return false;
        }
        HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (!::GetMonitorInfoW(mon, &mi)) {
            *err = L"取显示器信息失败";
            return false;
        }
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        const int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
        const int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
        ::SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return true;
    }

    *err = L"未知的窗口命令：" + command;
    return false;
}

bool DoExecute(const config::json& j, std::wstring* err) {
    const std::wstring target = ParamString(j, "Target");
    if (target.empty()) {
        *err = L"Execute 缺少 Target";
        return false;
    }
    if (target.find(L'%') != std::wstring::npos) {
        // 变量展开是 T-0016 的范围，这里如实说明而不是静默送出错误的目标。
        logger::Warn("Execute 的目标含未展开的变量（变量展开尚未实现，见 T-0016）：" +
                     WideToUtf8(target));
    }

    const bool runAs = ParamBool(j, "RunAs", false);
    const bool wait = ParamBool(j, "Wait", false);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = runAs ? L"runas" : L"open";
    sei.lpFile = target.c_str();
    sei.lpParameters = nullptr;
    sei.nShow = SW_SHOWNORMAL;

    if (!::ShellExecuteExW(&sei)) {
        const DWORD e = ::GetLastError();
        *err = L"启动失败（win32 错误 " + std::to_wstring(e) + L"）";
        return false;
    }
    if (wait && sei.hProcess != nullptr) {
        ::WaitForSingleObject(sei.hProcess, 30000);
    }
    if (sei.hProcess != nullptr) {
        ::CloseHandle(sei.hProcess);
    }
    return true;
}

bool DoSetClipboard(const std::wstring& text, std::wstring* err) {
    if (!::OpenClipboard(nullptr)) {
        *err = L"打开剪贴板失败（可能被其它程序占用）";
        return false;
    }
    if (!::EmptyClipboard()) {
        ::CloseClipboard();
        *err = L"清空剪贴板失败";
        return false;
    }
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem == nullptr) {
        ::CloseClipboard();
        *err = L"分配剪贴板内存失败";
        return false;
    }
    void* dst = ::GlobalLock(mem);
    if (dst == nullptr) {
        ::GlobalFree(mem);
        ::CloseClipboard();
        *err = L"锁定剪贴板内存失败";
        return false;
    }
    std::memcpy(dst, text.c_str(), bytes);
    ::GlobalUnlock(mem);
    if (::SetClipboardData(CF_UNICODETEXT, mem) == nullptr) {
        ::GlobalFree(mem);
        ::CloseClipboard();
        *err = L"写入剪贴板失败";
        return false;
    }
    // 所有权已交给剪贴板，不能再 GlobalFree。
    ::CloseClipboard();
    return true;
}

// 已知但本阶段未实现的动作：给出明确错误，而不是静默成功（规格要求「未知动作明确报错」）。
const wchar_t* UnimplementedTask(const std::wstring& type) {
    if (type == L"GetSelectedText") return L"T-0016";
    if (type == L"Screenshot" || type == L"Snapshot" || type == L"ScreenshotHQ" ||
        type == L"GetClipboard" || type == L"PinImage" || type == L"PictureInPicture")
        return L"T-0023/T-0024/T-0025";
    if (type == L"OCR") return L"T-0026";
    if (type == L"Text" || type == L"Algorithm") return L"T-0027";
    if (type == L"Volume" || type == L"Brightness" || type == L"SetBrightness") return L"T-0028";
    if (type == L"Explorer") return L"T-0030";
    if (type == L"PostMessage" || type == L"RegSet") return L"阶段 5";
    return nullptr;
}

}  // namespace

bool ResolveVirtualKey(const std::wstring& name, WORD* vk, bool* extended) {
    if (name.empty() || vk == nullptr || extended == nullptr) {
        return false;
    }
    const std::wstring u = Upper(name);

    // F1..F24
    if (u.size() >= 2 && u[0] == L'F') {
        bool allDigits = true;
        int n = 0;
        for (size_t i = 1; i < u.size(); ++i) {
            if (u[i] < L'0' || u[i] > L'9') {
                allDigits = false;
                break;
            }
            n = n * 10 + (u[i] - L'0');
        }
        if (allDigits && n >= 1 && n <= 24) {
            *vk = static_cast<WORD>(VK_F1 + n - 1);
            *extended = false;
            return true;
        }
    }

    if (u.size() == 1) {
        if (u[0] >= L'A' && u[0] <= L'Z') {
            *vk = u[0];
            *extended = false;
            return true;
        }
        if (u[0] >= L'0' && u[0] <= L'9') {
            *vk = u[0];
            *extended = false;
            return true;
        }
    }

    for (const KeyEntry& e : kKeys) {
        if (u == e.name) {
            *vk = e.vk;
            *extended = e.extended;
            return true;
        }
    }
    return false;
}

std::string Validate(const config::Action& action) {
    const std::wstring type = Utf8ToWide(action.type);
    if (type.empty()) {
        return "动作缺少 Type";
    }
    const config::json& j = action.raw;

    if (type == L"SendKeys") {
        const std::wstring keys = ParamString(j, "Keys");
        if (keys.empty()) return "SendKeys 缺少 Keys";
        WORD vk = 0; bool ext = false;
        // 复用真实解析路径做校验，避免校验与执行两套规则漂移
        std::wstring err;
        // 这里只校验最后一段（按键本身）
        std::wstring last = keys;
        const size_t pos = keys.find_last_of(L'+');
        if (pos != std::wstring::npos) last = Trim(keys.substr(pos + 1));
        if (!ResolveVirtualKey(last, &vk, &ext)) {
            return "SendKeys 无法识别的按键：" + WideToUtf8(last);
        }
        return {};
    }
    if (type == L"KeyDown" || type == L"KeyUp") {
        if (ParamString(j, "Keys").empty()) return "KeyDown/KeyUp 缺少 Keys";
        return {};
    }
    if (type == L"MouseClick") {
        return {};
    }
    if (type == L"MouseMove") {
        return {};
    }
    if (type == L"Execute") {
        if (ParamString(j, "Target").empty()) return "Execute 缺少 Target";
        return {};
    }
    if (type == L"Window") {
        if (ParamString(j, "Command").empty()) return "Window 缺少 Command";
        return {};
    }
    if (type == L"Delay") {
        const int ms = ParamInt(j, "Ms", -1);
        if (ms < 0 || ms > 60000) return "Delay 的 Ms 超出范围（0..60000）";
        return {};
    }
    if (type == L"SetClipboard") {
        if (!HasKey(j, "Text")) return "SetClipboard 缺少 Text";
        return {};
    }
    if (type == L"Internal") {
        const std::wstring cmd = ParamString(j, "Command");
        if (cmd.empty()) return "Internal 缺少 Command";
        const std::wstring c = Upper(cmd);
        if (c != L"EXIT" && c != L"TOGGLEPAUSE" && c != L"PAUSE" && c != L"RELOAD" &&
            c != L"SETTINGS" && c != L"CONFIG" && c != L"SHOWTIPS" && c != L"DELAY" &&
            c != L"EXCLUDE" && c != L"PRINT" && c != L"TEMPSTR" && c != L"ICON" &&
            c != L"CLIPBOARDMENU") {
            return "未知的 Internal 命令：" + WideToUtf8(cmd);
        }
        return {};
    }
    if (type == L"ShowTips") {
        return {};
    }
    if (type == L"ActivateWindow" || type == L"HideToTray" || type == L"RestoreFromTray") {
        return {};
    }

    const wchar_t* task = UnimplementedTask(type);
    if (task != nullptr) {
        return "动作尚未实现：" + action.type + "（计划任务 " + WideToUtf8(task) + "）";
    }
    return "未知动作类型：" + action.type;
}

int ExecuteChain(const std::vector<config::Action>& chain, const Context& ctx, HWND uiWindow) {
    // 先整体校验：一条链里有一个动作不合法就整条不执行，避免「执行到一半才发现参数错」。
    for (const config::Action& a : chain) {
        const std::string err = Validate(a);
        if (!err.empty()) {
            logger::Error("动作链校验失败：" + err);
            return 0;
        }
    }

    for (size_t i = 0; i < chain.size(); ++i) {
        const config::Action& a = chain[i];
        const std::wstring type = Utf8ToWide(a.type);
        const config::json& j = a.raw;
        std::wstring err;
        bool ok = true;

        if (type == L"SendKeys") {
            ok = SendKeysSpec(ParamString(j, "Keys"), &err);
        } else if (type == L"KeyDown") {
            ok = SendKeyState(ParamString(j, "Keys"), true, &err);
        } else if (type == L"KeyUp") {
            ok = SendKeyState(ParamString(j, "Keys"), false, &err);
        } else if (type == L"MouseClick") {
            ok = SendMouseClick(ParamString(j, "Button", L"Left"), &err);
        } else if (type == L"MouseMove") {
            const std::wstring t = Upper(ParamString(j, "Target"));
            if (t == L"START") {
                ::SetCursorPos(ctx.start.x, ctx.start.y);
            } else if (t == L"END") {
                ::SetCursorPos(ctx.end.x, ctx.end.y);
            } else {
                ::SetCursorPos(ParamInt(j, "X", ctx.end.x), ParamInt(j, "Y", ctx.end.y));
            }
        } else if (type == L"ActivateWindow") {
            if (!TargetAlive(ctx.targetWindow)) {
                err = L"目标窗口已失效";
                ok = false;
            } else {
                ::ShowWindow(ctx.targetWindow, SW_RESTORE);
                ::SetForegroundWindow(ctx.targetWindow);
            }
        } else if (type == L"Window") {
            ok = DoWindowCommand(ctx.targetWindow, ParamString(j, "Command"), &err);
        } else if (type == L"HideToTray") {
            ok = DoWindowCommand(ctx.targetWindow, L"HideTray", &err);
        } else if (type == L"RestoreFromTray") {
            ok = DoWindowCommand(ctx.targetWindow, L"RestoreFromTray", &err);
        } else if (type == L"Execute") {
            ok = DoExecute(j, &err);
        } else if (type == L"SetClipboard") {
            ok = DoSetClipboard(ParamString(j, "Text"), &err);
        } else if (type == L"Delay") {
            const int ms = std::clamp(ParamInt(j, "Ms", 0), 0, 60000);
            ::Sleep(static_cast<DWORD>(ms));
        } else if (type == L"ShowTips") {
            // 提示显示属于 UI：把文本交给主窗口，由它负责展示与释放。
            auto* text = new std::wstring(ParamString(j, "Text", L""));
            if (::PostMessageW(uiWindow, WM_APP_SHOW_TIP, reinterpret_cast<WPARAM>(text), 0) == FALSE) {
                delete text;
            }
        } else if (type == L"Internal") {
            const std::wstring c = Upper(ParamString(j, "Command"));
            UINT cmd = 0;
            if (c == L"EXIT") cmd = IDM_EXIT;
            else if (c == L"TOGGLEPAUSE" || c == L"PAUSE") cmd = IDM_TOGGLE_PAUSE;
            else if (c == L"RELOAD") cmd = IDM_RELOAD_CONFIG;
            else if (c == L"SETTINGS" || c == L"CONFIG") cmd = IDM_SETTINGS;
            else if (c == L"SHOWTIPS") {
                auto* text = new std::wstring(ParamString(j, "Text", L""));
                if (::PostMessageW(uiWindow, WM_APP_SHOW_TIP, reinterpret_cast<WPARAM>(text), 0) == FALSE) {
                    delete text;
                }
            } else {
                err = L"Internal 命令尚未实现：" + c;
                ok = false;
            }
            if (cmd != 0) {
                // 回到 UI 线程执行：这些命令会动窗口/托盘/配置，不能在动作线程直接做。
                ::PostMessageW(uiWindow, WM_APP_COMMAND, cmd, 0);
            }
        } else {
            const wchar_t* task = UnimplementedTask(type);
            err = task != nullptr ? (L"动作尚未实现：" + type + L"（" + task + L"）")
                                  : (L"未知动作类型：" + type);
            ok = false;
        }

        if (!ok) {
            logger::Error("动作[" + std::to_string(i) + "] " + a.type + " 执行失败：" +
                          WideToUtf8(err));
            return static_cast<int>(i);   // 顺序执行，失败即停
        }
    }
    return -1;
}

}  // namespace actions
