# 第三方组件与许可证

本工程按作者规格「不要为尚未开发的功能提前引入全部依赖……并记录第三方许可证」维护本文件。每引入一个依赖，在此登记其来源、版本、许可证与引入理由。

| 组件 | 版本 | 许可证 | 引入理由 | 形态 |
|---|---|---|---|---|
| **WTL**（Windows Template Library） | 10.0（`_WTL_VER 0x1000`） | **MS-PL** | 作者规格指定的窗口层（`CWindowImpl`、消息映射、`CMessageLoop`）；MouseInc 与本工程都用它 | vendored 头文件，`third_party/wtl/Include/`（20 个头文件），随包附 `MS-PL.txt` |
| **nlohmann/json** | 3.12.0 | **MIT**（源文件内 `SPDX-License-Identifier: MIT`） | 作者规格指定的配置解析库；用于读写 UTF-8 配置 JSON | 单头文件，`third_party/nlohmann/json.hpp`（953 KB），来自 https://github.com/nlohmann/json/releases/tag/v3.12.0 |
| **doctest** | 2.4.11 | **MIT** | 单元测试框架（作者规格「十七、验证与交付」要求自动化测试）。选型依据见 `docs/execution-plan.md` §2.2：单头文件、CMake/CTest 原生、模板开销最轻 | 单头文件，`third_party/doctest/doctest.h`（314 KB），来自 https://github.com/doctest/doctest/releases/tag/v2.4.11。**只编进测试 exe，不进发行物**（测试目标见 `CMakeLists.txt` 末尾） |

## WTL 的来源与许可证核实过程

- 来源：官方 WTL 10 正式版压缩包 `WTL10_10320_Final.zip`，取自镜像仓库 https://github.com/Win32-WTL/WTL 的 `Releases/` 目录（该仓库 README 声明官方站点为 https://sourceforge.net/projects/wtl/ 与 https://wtl.sourceforge.io ）。
- **许可证歧义已澄清**：该仓库自带的 `NuGet/WTL.nuspec` 里 `licenseUrl` 写的是 CPL 1.0，与源码头文件自称的 MS-PL 不一致。以正式发布包为准——压缩包根部带 `MS-PL.txt`，且 `Include/atlapp.h` 头部明确写着「Microsoft Public License (http://opensource.org/licenses/MS-PL) which can be found in the file MS-PL.txt at the root folder」。**结论：MS-PL**，`MS-PL.txt` 已随头文件一并保留在 `third_party/wtl/`。
- 本工程**不修改** WTL 源码，只作为头文件库包含。

## 只在本机存在、不随仓库分发的依赖

| 依赖 | 说明 |
|---|---|
| **ATL**（`atlwin.h` / `atlbase.h` 等） | 属于 Visual Studio 组件 `Microsoft.VisualStudio.Component.VC.ATL`，不是 WTL 的一部分。WTL 的 `atlapp.h`/`atlframe.h` 等扩展头必须配合 ATL 才能编译。装法与坑见 `CONTEXT.md` 的「本机开发环境」。 |

## 工具链（非分发物）

Visual Studio 2022 Build Tools（MSVC v143 / 14.44.35207）、Windows SDK 10.0.26100.7705、CMake 4.3.3。这些是构建工具，不随产物分发。

## 产物的运行期依赖

`mouselnk-plus.exe` 的导入表（实测）：`advapi32 / atlthunk / comctl32 / kernel32 / ole32 / oleaut32 / shell32 / user32`。

- **不含** `vcruntime140.dll` / `msvcp140.dll` —— 静态 CRT（`/MT`）生效，「无 VC++ 运行库依赖」成立。
- 含 **`atlthunk.dll`**：这是 Windows 自带组件（System32，Win10+ 起存在），因使用 ATL 的窗口 thunk 机制而引入。MouseInc.exe 2.13.4 的导入表里同样有它 —— 参考程序也是同一形态。它不算「需要用户安装的运行库」。
