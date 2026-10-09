// mouselnk-plus 进程入口。
//
// 这里只做进程级的三件事：COM 初始化、WTL 全局模块对象的建立与销毁、把控制权交给 CApp。
// 单实例、主窗口、托盘、消息循环都在 CApp（作者规格「二、项目架构」把这几项归在 App 模块）。

#include <windows.h>
#include <commctrl.h>

#include "App.h"

// WTL 的全局模块对象：所有 WTL 窗口类都通过它拿实例句柄。
CAppModule _Module;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*prevInstance*/, LPWSTR /*cmdLine*/,
                    int /*showCmd*/) {
    const HRESULT comResult =
        ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

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
