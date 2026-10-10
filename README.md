# mouselnk-plus

Windows 桌面鼠标手势 / 鼠标增强工具。项目名 `mouselnk-plus`（沿用目录名）。

**这是一个两段式目标的工程，两部分同等重要：**

1. **以 MouseInc 为骨架做忠实复现**——不只是「行为大致相似」，而是把它的稳定内核、功能面与配置语义尽量完整地重建出来。MouseInc 2.13.4 是这套骨架的基准（含 36 条手势模板、动作族与动作链、应用规则、边缘滚动、触发角、热键、截图/贴图/画中画/OCR、剪贴板菜单）。
2. **在保住这个稳定性的前提下，集成选定的 Aitiy 特性**——Aitiy（同一作者 2026 年的新作品）是**功能来源**，MouseInc 是**骨架与质量标准**。集成的具体清单由 owner 选定，见 `CONTEXT.md`。

一句话：**要 Aitiy 的功能，但不要它变重失稳的那部分代价。**

## 与 MouseInc / Aitiy 的关系（先读这段）

本工程**不包含、不使用、不反编译** MouseInc 或 Aitiy 的任何源码。

- MouseInc 本体闭源（只有设置界面开源，且 `shuax/MouseInc.Settings` 已于 2026-09-18 转只读），Aitiy 无公开仓库。
- 作者明确回复：不便提供源码，请从零独立实现，**使用自己的软件名称、图标、界面和服务地址**。
- 本机安装的两个程序只作为**行为参照（黑盒 oracle）**与本机取证对象：观察交互、读取它们释放到磁盘的 JSON 配置、比对功能清单。`CONTEXT.md` 记录了完整的取证清单与证据。

`mouselnk-plus` 是仓库/目录名，不是产品名。正式产品名称、图标、界面与服务地址均为自建，待定项见 `CONTEXT.md` 的「待确认」。

## Start here

1. `TODO.md` —— 当前任务与进度，接手时先看这个
2. `docs/execution-plan.md` —— **执行总纲**：工作原则、技术选型结论、标准流程、阶段任务地图、验证矩阵与风险登记
3. `AGENTS.md` —— 项目规则、依赖与技术选型纪律、复现的法律与工程边界
4. `CONTEXT.md` —— 已核实的领域事实、术语、本机取证证据、硬约束与已知失效模式
5. `temp/AGENTS.md` —— 往 `temp/` 放临时材料之前先读

## Development

**工具链**：C++20 + MSVC v143 + Windows SDK；工程系统用 CMake，工程描述里不锁生成器。

**依赖获取**（首次构建前）：

1. **Visual Studio 2022 Build Tools**，必须包含这两个组件：
   - `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`（MSVC 编译器）
   - `Microsoft.VisualStudio.Component.VC.ATL`（**ATL，WTL 的硬依赖**；缺它会在 `atlbase.h` 处编译失败）
   - 需要 Windows SDK（`Microsoft.VisualStudio.Component.Windows11SDK.26100`）
2. **WTL 10.0** 已 vendored 到 `third_party/wtl/`，无需另外获取；其许可证见 `THIRD-PARTY-NOTICES.md`。

**构建**（本机当前可用的路径）：

```bash
# 需要在已应用 vcvars64 的环境里执行
cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl
cmake --build build-ninja
```

> **为什么用 Ninja 而不是 Visual Studio 生成器**：本机的 MSBuild 无法定位已安装的 Windows SDK（`MSB8036`），任何 C++ 工程经 MSBuild 都会失败，与本工程无关 —— 详见 `CONTEXT.md` 的「本机开发环境」。CMake 工程本身不依赖生成器，等该机器问题修复后可直接改用：
>
> ```bash
> cmake -S . -B build -G "Visual Studio 17 2022" -A x64
> cmake --build build --config Release
> ```

产物目标：单文件、无运行库依赖（静态 CRT，`/MT`）的 exe，常驻系统托盘。

## 配置文件

| 模式 | 配置文件位置 | 如何进入 |
|---|---|---|
| 普通（默认） | `%APPDATA%\mouselnk-plus\config.json` | 默认行为 |
| 便携 | exe 同目录的 `mouselnk-plus.json` | 在 exe 同目录创建名为 `mouselnk-plus.portable` 的**标记文件** |

用显式标记文件而不是「目录可写就便携」，是为了让行为可预期（装在 Program Files 与放在 U 盘上表现一致）。

配置文件是 UTF-8 JSON，带 `SchemaVersion`，可直接手工编辑；改完在托盘菜单点「重载配置」即可生效。配置损坏时程序不会静默重置：原文件会改名为 `config.json.corrupt-<时间戳>` 留档，并弹窗告知。

## 当前状态

阶段 1 进行中，**核心手势链路已跑通**：按住右键划动 → 半透明浮层显示轨迹 → 松手后按模板识别 → 执行绑定动作。

- **程序外壳**：单实例、隐藏主窗口、托盘图标与菜单（设置 / 启用暂停 / 重载配置 / 开机启动 / 关于 / 退出）、Explorer 重启后恢复托盘、干净退出。
- **配置系统**：自带 schema 与 10 条默认手势、12 条全局绑定的默认配置，支持损坏备份、原子写入、热重载、便携模式。
- **输入**：全局低级鼠标钩子跑在专用消息泵线程上；右键手势六态状态机；普通右键照常弹菜单（吞下后补发）。
- **识别**：按作者规格实现的整体路径模板匹配，配 17 个用例 / 275 条断言的自动化测试（36 条模板全部可自我识别）。
- **浮层**：GDI+ 分层窗口，只覆盖轨迹包围盒，支持线宽/颜色/箭头与「成功 / 无匹配 / 无动作」三种反馈。
- **动作**：SendKeys、窗口操作、启动程序、剪贴板、延时、Internal 等基础动作，动作链在后台线程串行执行；未实现的动作会明确报错。

设置界面仍是占位（点「设置」会提示计划任务）；应用规则、热键、边缘滚动、热角、截图/贴图/OCR 等属于后续阶段 —— 进度见 `TODO.md`。

## 构建与测试

```bash
# 构建（需已应用 vcvars64）
cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl
cmake --build build-ninja

# 自动化测试（识别引擎）
./build-ninja/mouselnk-plus-tests.exe
# 或
cd build-ninja && ctest --output-on-failure
```

## Delivery notes

- 参考程序行为、官方手册、二进制指纹等证据见 `CONTEXT.md`。
- 上游调研全文在 `temp/research/`，作者技术路线原文在 `temp/input/`（均为本地材料，不入 Git）。
- 本地 MCP 配置与 `temp/` 载荷刻意排除在 Git 之外，边界见 `AGENTS.md`。
