#pragma once

#include <windows.h>

#include <string>
#include <vector>

// 透明轨迹浮层（作者规格「六、轨迹浮层与手势录制」）。
//
// 实现要点：
//   - 分层窗口（WS_EX_LAYERED + UpdateLayeredWindow），窗口**只覆盖轨迹的包围盒**
//     而不是整个虚拟桌面 —— 分层窗口的内存随面积走，全屏尺寸的 DIB 在多屏 4K 下
//     轻易就是几十 MB，而轨迹本身只有几百像素（见 docs/execution-plan.md 的风险登记）。
//   - 不抢焦点、不进任务栏、不阻挡鼠标：WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT。
//   - 按需重绘：只有点集变化时才重画，不跑渲染循环。
//   - 坐标全程用**物理像素**（进程已声明 PerMonitorV2），避免高 DPI 下轨迹偏移。
namespace ui {

// 浮层样式：只取绘制需要的字段，不把整个配置结构拖进来。
struct OverlayStyle {
    bool drawTrace   = true;
    bool drawResult  = true;
    bool traceArrow  = true;
    bool randColor   = false;
    int  traceWidth  = 3;
    int  fontSize    = 26;
    COLORREF drawColor = RGB(0xE4, 0x75, 0x42);   // 默认 #E47542
    COLORREF failColor = RGB(0xCA, 0xD0, 0xD3);   // 默认 #CAD0D3
};

class Overlay {
public:
    enum class ResultState {
        Success,    // 匹配到手势且有动作
        NoMatch,    // 没有匹配到模板
        NoAction,   // 匹配到了但没有绑定动作
    };

    static Overlay& Instance();

    bool Initialize(HINSTANCE instance);
    void Shutdown();

    // 一次手势的生命周期
    void BeginGesture();                       // 清空点集并隐藏
    void AddPoint(POINT p);                    // 追加一个轨迹点（有上限保护）
    void DrawLive(const OverlayStyle& style);  // 绘制中：按当前点集重绘
    void ShowResult(const OverlayStyle& style, ResultState state, const std::wstring& label);
    void Hide();

    bool hasWindow() const { return m_hwnd != nullptr; }
    int  pointCount() const { return static_cast<int>(m_points.size()); }

private:
    Overlay() = default;
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    // 按给定点集与配色绘制一次。label 非空时在终点附近画文字。
    void Render(const OverlayStyle& style, COLORREF color, const std::wstring& label);

    static constexpr int kMaxPoints = 4096;

    HWND    m_hwnd = nullptr;
    ULONG_PTR m_gdiplusToken = 0;
    bool    m_gdiplusReady = false;
    std::vector<POINT> m_points;
};

}  // namespace ui
