#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "Config.h"

// 统一动作引擎（作者规格「八、统一动作引擎」）。
//
// 手势、热键、边缘滚轮、热角、文本菜单将来复用同一套动作系统，所以这里只依赖
// 「动作链 + 上下文」，不关心触发源是谁。
//
// 执行模型：
//   - 一条动作链**顺序执行**，前一个没完成不进入下一个（规格要求）。
//   - 必须在**工作线程**调用（Delay / SendKeys / ShellExecute 都会阻塞），
//     绝不能跑在钩子回调或 UI 消息循环上（规格要求「不阻塞钩子和消息循环」）。
//   - 需要回到 UI 线程的动作（Internal 里的设置/重载/暂停/退出）通过向主窗口
//     PostMessage(WM_APP_COMMAND) 完成，不在工作线程直接碰 UI。
namespace actions {

// 动作执行上下文（作者规格列出的上下文项）。
struct Context {
    HWND         targetWindow = nullptr;   // 手势起始目标窗口（顶层窗口）
    DWORD        pid = 0;
    std::wstring processName;              // 如 chrome.exe（小写）
    std::wstring processPath;              // 完整路径
    POINT        start{};
    POINT        end{};
    RECT         bounds{};                 // 轨迹包围矩形
    std::wstring tempText;                 // %tempstr% 的当前值
};

// 校验单个动作的类型与参数（规格：校验类型、参数类型、数量和范围；未知动作明确报错）。
// 返回空串表示通过，否则是面向用户的错误说明。
std::string Validate(const config::Action& action);

// 顺序执行一条动作链。返回**第一个失败动作的下标**（-1 表示全部成功）。
// uiWindow：Internal 类动作要 post 回去的主窗口。
int ExecuteChain(const std::vector<config::Action>& chain, const Context& ctx, HWND uiWindow);

// 解析 SendKeys 的按键名（"PgUp" / "F5" / "A" / "Back" …）。
// 暴露出来是为了让诊断日志与将来的录制功能复用同一张表，避免两处漂移。
bool ResolveVirtualKey(const std::wstring& name, WORD* vk, bool* extended);

}  // namespace actions
