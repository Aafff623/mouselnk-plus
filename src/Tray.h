#pragma once

#include <windows.h>
#include <shellapi.h>

#include "resource.h"

// 托盘图标与右键菜单。
//
// 只负责「图标状态 + 菜单弹出」，不含业务逻辑：菜单选中的命令交回 CApp 执行。
// Explorer 重启后托盘会被清空，此时需要重新 Add —— 由 CApp 处理 TaskbarCreated 后调用。
class CTray {
public:
    // 暂停状态体现在图标与提示文本上（作者规格「三、程序基础行为」）。
    bool Add(HWND hwnd, HINSTANCE instance, bool paused);
    void Remove();
    bool SetPaused(bool paused);

    // 在鼠标位置弹出托盘菜单，返回被选中的命令 ID（未选中返回 0）。
    UINT PopupMenu(bool paused, bool autostartEnabled);

    // 「已在运行」提示（第二个实例被拦截时调用）。
    void NotifyAlreadyRunning() const;

    bool IsAdded() const { return m_added; }

    // 供崩溃兜底路径复制一份，用于进程异常退出时摘掉图标。
    const NOTIFYICONDATAW& Data() const { return m_nid; }

private:
    bool Apply(bool paused);   // 写入/更新 NOTIFYICONDATAW

    NOTIFYICONDATAW m_nid{};
    HICON m_iconNormal = nullptr;
    HICON m_iconPaused = nullptr;
    bool  m_added = false;
};
