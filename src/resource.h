// 资源 ID、托盘命令 ID 与自定义消息。
// 本文件同时被 .rc 与 C++ 源码包含，因此只放宏定义。
#pragma once

// ---- 图标资源 ----
// 占位图标：由 temp/scripts/make-placeholder-icons.py 生成，等 owner 提供正式图标后替换。
#define IDI_APP                 101
#define IDI_APP_PAUSED          102

// ---- 内嵌资源 ----
// 默认配置 JSON：首次运行时释放到配置文件位置。内容为 UTF-8，RCDATA 原样嵌入不做代码页转换。
#define IDR_DEFAULT_CONFIG      201

// ---- 托盘菜单命令 ----
#define IDM_SETTINGS            40001
#define IDM_TOGGLE_PAUSE        40002
#define IDM_RELOAD_CONFIG       40003
#define IDM_AUTOSTART           40004
#define IDM_ABOUT               40005
#define IDM_EXIT                40006

// ---- 自定义消息（WM_APP 段，作者规格要求模块间用 PostMessage + 自定义 WM_APP 通信）----
#define WM_APP_TRAY             (WM_APP + 1)  // Shell_NotifyIcon 回调消息
#define WM_APP_COMMAND          (WM_APP + 2)  // 跨线程投递命令，wParam = IDM_*
#define WM_APP_SECOND_INSTANCE  (WM_APP + 3)  // 第二个实例启动时通知已有实例
#define WM_APP_GESTURE_DONE     (WM_APP + 4)  // 钩子线程：有手势结果可取
#define WM_APP_REINJECT_CLICK   (WM_APP + 5)  // 钩子线程 → App：请补发一对普通右键（wParam=x, lParam=y）
#define WM_APP_TRACE_POINT      (WM_APP + 6)  // 钩子线程 → App：轨迹新增一个点（wParam=x, lParam=y）
#define WM_APP_SHOW_TIP         (WM_APP + 7)  // 动作线程 → App：显示一条提示（wParam = std::wstring*，接收方负责 delete）
