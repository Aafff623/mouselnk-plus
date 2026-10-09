// mouselnk-plus 进程入口。
//
// 这里只做进程级的三件事：COM 初始化、WTL 全局模块对象的建立与销毁、把控制权交给 CApp。
// 单实例、主窗口、托盘、消息循环都在 CApp（作者规格「二、项目架构」把这几项归在 App 模块）。
//
// 另外提供 `--selftest`：用脚本化的输入序列跑一遍手势状态机并把结果写进日志。
// 存在的理由：验证状态机不需要真的动鼠标（也避免干扰用户桌面），而「状态迁移与轨迹点数
// 能写进日志」正是 T-0004 的验收断言。

#include <windows.h>
#include <commctrl.h>

#include <filesystem>
#include <string>

#include "App.h"
#include "Config.h"
#include "GestureState.h"
#include "Log.h"

// WTL 的全局模块对象：所有 WTL 窗口类都通过它拿实例句柄。
CAppModule _Module;

namespace {

int g_failedChecks = 0;

std::wstring ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                                           nullptr, 0);
    if (need <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), need);
    return out;
}

// 一条用例的结果汇总
struct Check {
    const char* name;
    bool        pass;
    std::string detail;
};

void Report(const Check& c) {
    const std::string line = std::string(c.pass ? "[PASS] " : "[FAIL] ") + c.name + " —— " + c.detail;
    if (c.pass) {
        logger::Info(line);
    } else {
        logger::Error(line);
        ++g_failedChecks;
    }
}

// 跑完一个用例后把状态机里剩下的结果取出来（正常情况下至多一条）
bool TakeOne(gesture::Machine& m, gesture::Trace* out) {
    return m.TakeCompleted(out);
}

int RunGestureSelfTest() {
    config::Store::Instance().Load();     // 顺带保证默认配置存在
    const std::filesystem::path logPath =
        std::filesystem::path(config::Store::Instance().Path()).parent_path() / L"mouselnk-plus.log";
    logger::Init(logPath);
    logger::Info("=== 手势状态机自检开始 ===");

    gesture::Settings s;
    s.startDistance = 5;       // 物理像素
    s.timeoutMs = 1000;
    s.restoreOnFailure = false;

    // ---- 用例 1：未达触发距离 → 普通右键，需补发 ----
    {
        gesture::Machine m;
        m.Configure(s);
        m.OnRightDown({100, 100}, 1000);
        m.OnMove({102, 100}, 1010);            // 距起点 2px < 5px
        const bool consumed = m.OnRightUp({102, 100}, 1020);
        gesture::Trace t;
        const bool hasTrace = TakeOne(m, &t);
        Report({"未达触发距离应补发普通右键",
                consumed && m.TakeReinjectRequest() && !hasTrace,
                "消费=" + std::string(consumed ? "是" : "否") +
                    " 请求补发=是 产生轨迹=" + std::string(hasTrace ? "是（不应有）" : "否")});
    }

    // ---- 用例 2：达到触发距离 → 产出轨迹 ----
    {
        gesture::Machine m;
        m.Configure(s);
        m.OnRightDown({100, 100}, 2000);
        m.OnMove({120, 100}, 2010);            // 20px ≥ 5px → 进入绘制
        m.OnMove({140, 100}, 2020);
        m.OnRightUp({140, 100}, 2030);
        gesture::Trace t;
        const bool hasTrace = TakeOne(m, &t);
        const bool ok = hasTrace && t.kind == gesture::Kind::Trace && t.pointCount >= 3 &&
                        m.idle() && !m.TakeReinjectRequest();
        Report({"达到触发距离应产出轨迹", ok,
                hasTrace ? gesture::FormatTrace(t) : std::string("没有轨迹")});
    }

    // ---- 用例 3：右键 + 滚轮 → 伪手势，且不弹右键菜单 ----
    {
        gesture::Machine m;
        m.Configure(s);
        m.OnRightDown({300, 300}, 3000);
        const bool wheelConsumed = m.OnWheel(120, 3010);         // 向上滚
        const bool upConsumed = m.OnRightUp({300, 300}, 3020);   // 吞掉抬起 → 不弹菜单
        gesture::Trace t;
        const bool hasTrace = TakeOne(m, &t);
        const bool ok = wheelConsumed && upConsumed && hasTrace &&
                        t.kind == gesture::Kind::WheelUp && t.finalState == gesture::State::WheelCombo;
        Report({"右键滚轮应为伪手势且不弹菜单", ok,
                hasTrace ? gesture::FormatTrace(t) : std::string("没有结果")});
    }

    // ---- 用例 4：移动停顿超时 → 取消（RestoreOnFailure=false 不补发）----
    {
        gesture::Machine m;
        m.Configure(s);
        m.OnRightDown({400, 400}, 4000);
        m.OnMove({430, 400}, 4010);            // 进入绘制
        const bool cancelled = m.CheckTimeout(4010 + s.timeoutMs + 1);
        gesture::Trace t;
        const bool hasTrace = TakeOne(m, &t);
        const bool ok = cancelled && hasTrace && t.finalState == gesture::State::Cancelled &&
                        !m.TakeReinjectRequest();
        Report({"移动停顿超时应取消且不补发", ok,
                hasTrace ? gesture::FormatTrace(t) : std::string("没有结果")});
    }

    // ---- 用例 5：停顿超时 + RestoreOnFailure=true → 补发恢复 ----
    {
        gesture::Settings s2 = s;
        s2.restoreOnFailure = true;
        gesture::Machine m;
        m.Configure(s2);
        m.OnRightDown({500, 500}, 5000);
        m.OnMove({530, 500}, 5010);
        const bool cancelled = m.CheckTimeout(5010 + s2.timeoutMs + 1);
        const bool askReinject = m.TakeReinjectRequest();
        Report({"超时且 RestoreOnFailure=true 时应补发", cancelled && askReinject,
                "已取消=" + std::string(cancelled ? "是" : "否") +
                    " 请求补发=" + std::string(askReinject ? "是" : "否")});
    }

    // ---- 用例 6：未按右键时的滚轮应放行 ----
    {
        gesture::Machine m;
        m.Configure(s);
        const bool consumed = m.OnWheel(120, 6000);
        Report({"未按右键的滚轮应放行", !consumed,
                std::string("消费=") + (consumed ? "是（不应消费）" : "否")});
    }

    // ---- 用例 7：长按但不动 → 仍是普通右键（超时只算移动停顿）----
    {
        gesture::Machine m;
        m.Configure(s);
        m.OnRightDown({600, 600}, 7000);
        const bool timedOut = m.CheckTimeout(7000 + s.timeoutMs * 3);   // 长时间没动
        const bool consumed = m.OnRightUp({600, 600}, 7000 + s.timeoutMs * 3 + 10);
        gesture::Trace t;
        const bool hasTrace = TakeOne(m, &t);
        Report({"按住右键不动不算超时，仍应补发普通右键",
                !timedOut && consumed && m.TakeReinjectRequest() && !hasTrace,
                "误判超时=" + std::string(timedOut ? "是" : "否") +
                    " 请求补发=" + std::string(!hasTrace ? "是" : "否")});
    }

    logger::Info("=== 自检结束；失败 " + std::to_string(g_failedChecks) + " 项 ===");
    logger::Shutdown();
    // 不弹窗：自检的结论以退出码（0=全通过）与日志文件为准，避免阻塞自动化调用。
    return g_failedChecks == 0 ? 0 : 1;
}

bool HasSelfTestFlag() {
    const wchar_t* cmd = ::GetCommandLineW();
    return cmd != nullptr && ::wcsstr(cmd, L"--selftest") != nullptr;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*prevInstance*/, LPWSTR /*cmdLine*/,
                    int /*showCmd*/) {
    const HRESULT comResult =
        ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    if (HasSelfTestFlag()) {
        const int code = RunGestureSelfTest();
        if (SUCCEEDED(comResult)) {
            ::CoUninitialize();
        }
        return code;
    }

    ::AtlInitCommonControls(ICC_STANDARD_CLASSES | ICC_BAR_CLASSES);

    _Module.Init(nullptr, instance);

    int exitCode = 1;
    CApp& app = CApp::Instance();

    switch (app.Initialize(instance)) {
        case CApp::StartResult::Ok:
            exitCode = app.Run();
            app.Shutdown();
            break;

        case CApp::StartResult::AlreadyRunning:
            // 已有实例，Initialize 已经通知过它；本实例安静退出。
            exitCode = 0;
            break;

        case CApp::StartResult::Failed:
            ::MessageBoxW(nullptr, L"启动失败：无法创建主窗口或托盘图标。",
                          L"mouselnk-plus", MB_OK | MB_ICONERROR);
            exitCode = 1;
            break;
    }

    _Module.Term();

    if (SUCCEEDED(comResult)) {
        ::CoUninitialize();
    }

    return exitCode;
}
