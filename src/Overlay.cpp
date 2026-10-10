#include "Overlay.h"

#include <objidl.h>   // GDI+ 需要 IStream 的定义，必须在 gdiplus.h 之前
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Log.h"

namespace ui {
namespace {

const wchar_t* kOverlayClass = L"mouselnk-plus.OverlayWindow";

Gdiplus::Color ToColor(COLORREF c, BYTE alpha = 255) {
    // 注意：GetRValue/GetGValue/GetBValue 是 wingdi.h 的函数式宏，
    // 不能加 :: 限定（会被展开成 ::((BYTE)(c)) 而报语法错误）。
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

// RandColor 配置：色相随机，但固定高饱和、满亮度，避免画出看不清的暗色。
COLORREF RandomBrightColor() {
    static std::uint32_t state = 0x12345678u;
    state = state * 1664525u + 1013904223u + ::GetTickCount();

    const double h = static_cast<double>(state % 360u);
    const double s = 0.85;
    const double v = 1.0;
    const double c = v * s;
    const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = v - c;

    double r = 0.0, g = 0.0, b = 0.0;
    if (h < 60.0)       { r = c; g = x; }
    else if (h < 120.0) { r = x; g = c; }
    else if (h < 180.0) { g = c; b = x; }
    else if (h < 240.0) { g = x; b = c; }
    else if (h < 300.0) { r = x; b = c; }
    else                { r = c; b = x; }

    return RGB(static_cast<BYTE>((r + m) * 255.0),
               static_cast<BYTE>((g + m) * 255.0),
               static_cast<BYTE>((b + m) * 255.0));
}

}  // namespace

Overlay& Overlay::Instance() {
    static Overlay instance;
    return instance;
}

LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) {
        // 双保险：WS_EX_TRANSPARENT 已经让鼠标穿透，这里再显式声明一次。
        return HTTRANSPARENT;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool Overlay::Initialize(HINSTANCE instance) {
    if (m_hwnd != nullptr) {
        return true;
    }

    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &input, nullptr) != Gdiplus::Ok) {
        logger::Error("GDI+ 初始化失败，轨迹浮层不可用");
        return false;
    }
    m_gdiplusReady = true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Overlay::WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kOverlayClass;
    if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        logger::Error("注册浮层窗口类失败，win32 错误 " + std::to_string(::GetLastError()));
        return false;
    }

    // 分层 + 穿透 + 不抢焦点 + 不进任务栏/Alt-Tab + 置顶
    m_hwnd = ::CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        kOverlayClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    if (m_hwnd == nullptr) {
        logger::Error("创建浮层窗口失败，win32 错误 " + std::to_string(::GetLastError()));
        return false;
    }
    return true;
}

void Overlay::Shutdown() {
    if (m_hwnd != nullptr) {
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_gdiplusReady) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusReady = false;
    }
    m_points.clear();
}

void Overlay::BeginGesture() {
    m_points.clear();
    Hide();
}

void Overlay::AddPoint(POINT p) {
    if (static_cast<int>(m_points.size()) >= kMaxPoints) {
        return;
    }
    // 丢弃与上一个点完全相同的点，减少无意义的重复（不影响识别，识别自己也会去重）。
    if (!m_points.empty() && m_points.back().x == p.x && m_points.back().y == p.y) {
        return;
    }
    m_points.push_back(p);
}

void Overlay::DrawLive(const OverlayStyle& style) {
    if (!style.drawTrace) {
        return;
    }
    const COLORREF color = style.randColor ? RandomBrightColor() : style.drawColor;
    Render(style, color, L"");
}

void Overlay::ShowResult(const OverlayStyle& style, ResultState state, const std::wstring& label) {
    COLORREF color = style.drawColor;
    std::wstring text;
    switch (state) {
        case ResultState::Success:
            text = style.drawResult ? label : std::wstring();
            break;
        case ResultState::NoMatch:
            color = style.failColor;
            break;
        case ResultState::NoAction:
            color = style.failColor;
            text = L"无动作";
            break;
    }
    Render(style, color, text);
}

void Overlay::Hide() {
    if (m_hwnd != nullptr && ::IsWindowVisible(m_hwnd)) {
        ::ShowWindow(m_hwnd, SW_HIDE);
    }
}

void Overlay::Render(const OverlayStyle& style, COLORREF color, const std::wstring& label) {
    if (m_hwnd == nullptr || !m_gdiplusReady) {
        return;
    }
    if (m_points.size() < 2) {
        Hide();
        return;
    }

    const int pen = std::max(1, style.traceWidth);
    const int fontSize = std::max(8, style.fontSize);

    // ---- 1. 轨迹包围盒（物理像素）----
    LONG left = m_points[0].x, right = left, top = m_points[0].y, bottom = top;
    for (const POINT& p : m_points) {
        left = std::min(left, p.x);
        right = std::max(right, p.x);
        top = std::min(top, p.y);
        bottom = std::max(bottom, p.y);
    }

    // ---- 2. 文案（画在轨迹下方）参与包围盒计算 ----
    Gdiplus::Font font(L"Microsoft YaHei UI", static_cast<Gdiplus::REAL>(fontSize),
                       Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    bool hasText = !label.empty();
    LONG textX = 0, textY = 0, textW = 0, textH = 0;
    if (hasText) {
        Gdiplus::Bitmap probe(1, 1, PixelFormat32bppPARGB);
        Gdiplus::Graphics mg(&probe);
        Gdiplus::RectF measured;
        mg.MeasureString(label.c_str(), -1, &font, Gdiplus::PointF(0.0f, 0.0f), &measured);
        textW = static_cast<LONG>(std::ceil(measured.Width)) + 6;
        textH = static_cast<LONG>(std::ceil(measured.Height)) + 6;
        textX = left;
        textY = bottom + 6;

        left = std::min(left, textX);
        top = std::min(top, textY);
        right = std::max(right, textX + textW);
        bottom = std::max(bottom, textY + textH);
    }

    const LONG margin = pen + 6;
    left -= margin;
    top -= margin;
    right += margin;
    bottom += margin;

    const LONG w = right - left;
    const LONG h = bottom - top;
    // 上限保护：异常轨迹不应产生巨型 DIB（分层窗口内存随面积走）。
    if (w <= 0 || h <= 0 || w > 20000 || h > 20000) {
        return;
    }

    HDC screen = ::GetDC(nullptr);
    if (screen == nullptr) {
        return;
    }
    HDC mem = ::CreateCompatibleDC(screen);
    if (mem == nullptr) {
        ::ReleaseDC(nullptr, screen);
        return;
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // 负数 = top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib == nullptr || bits == nullptr) {
        ::DeleteDC(mem);
        ::ReleaseDC(nullptr, screen);
        return;
    }
    HGDIOBJ oldBmp = ::SelectObject(mem, dib);

    // ---- 3. 用 GDI+ 直接画进 DIB 的位（PARGB，供 UpdateLayeredWindow 使用）----
    {
        // Bitmap 直接包住 DIB 的位，画完即写入，省一次拷贝。
        Gdiplus::Bitmap bmp(static_cast<INT>(w), static_cast<INT>(h),
                            static_cast<INT>(w * 4), PixelFormat32bppPARGB,
                            static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));

        std::vector<Gdiplus::Point> pts;
        pts.reserve(m_points.size());
        for (const POINT& p : m_points) {
            pts.emplace_back(p.x - left, p.y - top);
        }

        Gdiplus::Pen tracePen(ToColor(color), static_cast<Gdiplus::REAL>(pen));
        tracePen.SetStartCap(Gdiplus::LineCapRound);
        tracePen.SetEndCap(Gdiplus::LineCapRound);
        tracePen.SetLineJoin(Gdiplus::LineJoinRound);
        if (pts.size() >= 2) {
            g.DrawLines(&tracePen, pts.data(), static_cast<INT>(pts.size()));
        }

        // 箭头：沿最后一段方向画一个 V
        if (style.traceArrow && pts.size() >= 2) {
            const Gdiplus::Point& a = pts[pts.size() - 2];
            const Gdiplus::Point& b = pts.back();
            double dx = static_cast<double>(b.X - a.X);
            double dy = static_cast<double>(b.Y - a.Y);
            const double len = std::hypot(dx, dy);
            if (len > 1e-6) {
                dx /= len;
                dy /= len;
                const double alen = std::max(9.0, pen * 3.5);
                const double ang = 0.5;   // 约 28°
                const double c = std::cos(ang), s = std::sin(ang);
                const double bx = -dx, by = -dy;   // 反方向
                const Gdiplus::Point p1(
                    b.X + static_cast<INT>(std::lround((bx * c - by * s) * alen)),
                    b.Y + static_cast<INT>(std::lround((bx * s + by * c) * alen)));
                const Gdiplus::Point p2(
                    b.X + static_cast<INT>(std::lround((bx * c + by * s) * alen)),
                    b.Y + static_cast<INT>(std::lround((-bx * s + by * c) * alen)));
                g.DrawLine(&tracePen, b, p1);
                g.DrawLine(&tracePen, b, p2);
            }
        }

        if (hasText) {
            Gdiplus::SolidBrush brush(ToColor(color));
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentNear);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
            const Gdiplus::RectF layout(static_cast<Gdiplus::REAL>(textX - left),
                                        static_cast<Gdiplus::REAL>(textY - top),
                                        static_cast<Gdiplus::REAL>(textW),
                                        static_cast<Gdiplus::REAL>(textH));
            g.DrawString(label.c_str(), -1, &font, layout, &fmt, &brush);
        }
    }   // bmp / g 析构，位已定稿

    // ---- 4. 一次性把整张位图交给系统合成 ----
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    POINT dst{left, top};
    POINT src{0, 0};
    SIZE size{w, h};
    ::UpdateLayeredWindow(m_hwnd, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);

    ::SelectObject(mem, oldBmp);
    ::DeleteObject(dib);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);

    if (!::IsWindowVisible(m_hwnd)) {
        ::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    }
}

}  // namespace ui
