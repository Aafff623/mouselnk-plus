# ADR-0001: 编译工具链与工程系统选型

- Status: accepted
- Date: 2026-10-08

## Context

作者的复现路线指定：C++20 + Visual Studio 2022 + MSVC v143 + Windows SDK。

本机实测结果与这条路线不符：没有安装 Visual Studio（注册表无 VS 15/16/17 安装项，`C:\Program Files\Microsoft Visual Studio` 与 `Program Files (x86)\Windows Kits` 均不存在，全盘未找到 MSVC 的 `cl.exe`）。现成的 C++ 能力是 MinGW-w64 g++ 16.1.0（UCRT, posix-seh）、CMake 4.3.3、Ninja。

这项选择会决定整个工程的构建描述形式、可用的窗口/UI 基础设施，以及后续所有编译相关工作的可行性，属于难以逆转的结构性决策。

## Decision

1. **安装 VS2022 Build Tools**，工作负载 `Microsoft.VisualStudio.Workload.VCTools`（含 MSVC v143 + Windows SDK），通过 winget 静默安装。
2. **工程系统用 CMake**，主生成器 `Visual Studio 17 2022`，平台 x64；不手写 `.vcxproj` / `.sln`。
3. 运行时目标：**静态 CRT（`/MT`）**，产物为单文件、无运行库依赖的便携 exe。

## Consequences

变得更可行：

- 严格贴合作者路线，C++20 语言特性与 MSVC 工具链行为都按作者预期。
- **ATL/WTL 可用**。这是关键的实际收益：本机两个参考程序的 PE 导入表里都有 `atlthunk.dll`，说明 ATL/WTL 是该品类的现实底座；`atlthunk` 是 MSVC 专属运行时组件，MinGW 下基本不可用。
- 静态 CRT + MSVC 是达成「单文件、无运行库依赖」最直接的路径，与参考程序的特征（MouseInc.exe 不导入 `vcruntime140.dll` / `msvcp140.dll`）一致。
- CMake 把工程配置集中在一处，`build/` 下的 `.vcxproj` 是可再生产物而非需要维护的源文件。

代价与约束：

- 一次性成本：约 4–6 GB 下载与相应的磁盘占用，安装期间无法编译。
- 生成器留在 CMake 里，不锁定编译器：日后仍可切 MinGW/Ninja。代价是源码要避免 MSVC 专有扩展（除非真的需要），并在用到 ATL/WTL 时接受该部分不可移植。

## Alternatives considered

**MinGW-w64 g++ + Ninja（本机现成）**
零等待，当天就能编译出单文件 exe，CMake 工程照样写。被否决的原因：偏离作者指定的编译器；ATL/WTL 基本不可用，而两个参考程序都用它，等于主动放弃对标的实现底座；后续若因需要 ATL 而切回 MSVC，窗口层要返工。

**改用 Rust 重写**
无需安装 VS，内存安全，原生单文件产物，包管理与测试比 C++ 省心。被否决的原因：完全偏离作者明确指定的 C++ 路线；本机两份参考二进制的架构线索（ATL/WTL 窗口层、Direct2D + DirectComposition 自研 UI）无法复用；Win32 低级钩子、COM、DWM 缩略图这一带的成熟 C++ 参考与示例远多于 Rust，会对复现速度和正确性都不利。
