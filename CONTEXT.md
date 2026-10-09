# mouselnk-plus Context

本文件保存**已核实的事实**，供 Agent 在本工程中工作时使用。它不是产品介绍，也不是猜测的存放处。每条结论标注来源或取证方式；未实测的推断一律显式标为「推测」。

## Purpose and boundaries

`mouselnk-plus` 是**两段式目标**的工程，两部分同等重要：

1. **以 MouseInc 为骨架做忠实复现**——重建它的稳定内核、完整功能面与配置语义，基准是 MouseInc 2.13.4。
2. **在保住这个稳定性的前提下，集成选定的 Aitiy 特性**——Aitiy 是**功能来源**，MouseInc 是**骨架与质量标准**。要 Aitiy 的功能，不要它变重失稳的那部分代价。

边界：

- 本工程**不做** MouseInc/Aitiy 二进制的反编译、反汇编或逐字节复制（作者明确要求独立实现，并使用自己的名称、图标、界面和服务地址）。本机两个程序是**行为参照（黑盒 oracle）**与取证对象，不是代码来源。
- 「复原」的验收标准是**可观察行为与架构分层一致**，不是二进制产物一致。
- 合法的骨架来源见 `AGENTS.md` 的「『复原 MouseInc』的合法来源」一节——其中 `sanan2015/MouseInc`（MIT，v2.1 完整源码）是**唯一的真源码参考**，用于工程结构、钩子处理与手势引擎的组织方式。

## 作者给定的技术路线（需求基准，已确定）

原文全文在 `temp/input/mouseinc-author-spec.md`。以下是已定的事项，实现时不要再自行选型。

| 方面 | 已定方案 |
|---|---|
| 语言/工具链 | C++20、MSVC v143、Windows SDK、Unicode 字符集、**首先支持 Windows 10/11 x64** |
| 工程形式 | 完整 `.sln`/`.vcxproj`，**或可生成 Visual Studio 工程的 CMake 项目**（本工程选 CMake，见 ADR-0001） |
| 资源嵌入 | `.rc` 文件嵌入图标、默认 JSON、图片与 WAV 音效 |
| manifest | 声明 Common Controls v6 与 **PerMonitorV2** DPI 感知 |
| 窗口层 | Win32 API + **ATL/WTL**（`CWindowImpl`、消息映射、`CMessageLoop`）；隐藏主窗口处理托盘/重载/模块消息；`PostMessage` + 自定义 `WM_APP` 通信；托盘用 `Shell_NotifyIcon`；处理 `TaskbarCreated` |
| 输入 | `SetWindowsHookEx` 装 `WH_MOUSE_LL` + `WH_KEYBOARD_LL`；未消费的交 `CallNextHookEx`；`SendInput` 模拟输入；**用自定义 `dwExtraInfo` 标记自身模拟输入以防递归触发**；钩子回调禁止网络请求、长时间等待、复杂绘制 |
| 绘制 | GDI+（`Graphics`/`GraphicsPath`/`Pen`/`Brush`/`Font`/`Bitmap`）+ GDI（内存 DC、位图、像素缓冲）；透明浮层 `WS_EX_LAYERED` + `UpdateLayeredWindow`；配合 `WS_EX_TOPMOST`/`WS_EX_TOOLWINDOW`/`WS_EX_TRANSPARENT` 与不激活窗口的显示方式；抗锯齿、按需重绘、上限约 60 FPS |
| 配置 | **nlohmann/json**，UTF-8 |
| 设置界面 | **Microsoft Edge WebView2** + 本地 HTML/CSS/JS；COM 用 WRL/WIL 等 RAII 封装；优先用 WebView2 消息桥交换 JSON；若走本地 HTTP/WebSocket 则用 **Mongoose**、仅监听回环、限制来源；资源随包打包、**断网可用** |
| 截图/图片 | GDI `BitBlt` 用 `SRCCOPY \| CAPTUREBLT`；`CreateDIBSection` 管像素；GDI+ `Bitmap` 做缩放与 PNG/JPEG 编码；COM `IStream` 内存编码；`GetCursorInfo` + `DrawIconEx` 画指针 |
| 画中画 | **DWM 缩略图三件套**：`DwmRegisterThumbnail` / `DwmUpdateThumbnailProperties` / `DwmUnregisterThumbnail` |
| 系统能力 | `ShellExecuteEx`（启动/打开/网址）、Win32 窗口 API、Shell API 定位文件、`CreateMutex` 单实例、Win32 Clipboard API、**WinINet** 网络、COM/WMI 硬件亮度、`SetDeviceGammaRamp` 软件调暗、WinMM/MCI 音效 |
| 第三方库 | **Nayuki QR-Code-generator**（二维码）、**LibTomCrypt + LibTomMath**（摘要与签名验证）、**miniz**（ZIP 解压） |

**依赖引入纪律**（规格明确要求）：不要为尚未开发的功能提前引入全部依赖；先集成当前阶段需要的库，后续按模块增加，并**记录第三方许可证**。

### 作者规格与本机实测的关系

一致处（实测印证了规格）：

- 画中画用 DWM 缩略图 —— 与实测发现 MouseInc.exe 内含 `DwmRegisterThumbnail` 完全吻合。
- 手势识别是**模板轨迹匹配**而非方向字符串压缩 —— 与「MouseInc/Aitiy 手势数据是同一套归一化坐标、36 条模板一一对应」的实测结论吻合。
- StartDistance 5 / TimeoutMs 1000 —— 与用户实际 `MouseInc.json`（`StartDistance: 5`、`Timeout: 1000`）一致。
- 静态 CRT、单文件便携 —— 与 MouseInc.exe 不导入 `vcruntime140`/`msvcp140` 的实测一致。

**冲突处（以实测为准，规格的数字不作为对齐目标）**：

| 项 | 作者规格 | 本机实测 | 处理 |
|---|---|---|---|
| 灵敏度默认值 | `Sensitivity: 50`，范围 0～100 | 用户实际配置是 `Sensitive: 100`（键名也不同） | 按规格实现（50 为默认、键名 `Sensitivity`）；实测值只说明用户偏好，不是对齐目标 |
| 配置 schema | 新设计：`SchemaVersion` + `MouseGesture` + `Gestures[].{Id,Points}` + `MatchGlobal[].{GestureId,...}` + 动作对象 `{"Type":"SendKeys","Keys":"Ctrl+C"}` | MouseInc 实际：扁平 PascalCase，`Gestures[].{Sign,Data}`（Data 是扁平数组），`MatchGlobal[].{Sign,Name,Actions}`，动作是数组 `["SendKeys","Ctrl+C"]` | **采用作者新 schema**（ADR-0002）。两者字段名、嵌套与动作表示都不同，**不是兼容格式** |
| 模板坐标 | `Points` 为点对数组 `[[0,100],[0,0]]` | MouseInc 为扁平数组 `[0,100,0,0]`；Aitiy 为 `"0,100 0,0"` 字符串 | 三者语义相同，序列化形式不同；内部用规格的点对数组 |

## Domain vocabulary

| 术语 | 在本工程中的确定含义 |
|---|---|
| **Sign（手势签名）** | 手势的字符串标识，如 `UP`、`DOWN-RIGHT`、`/ UP`、`WheelSwitchUp`。它既是识别结果，也是规则表的键。 |
| **手势模板（Data / path）** | 一条归一化轨迹的参考点序列。MouseInc 用扁平坐标数组 `[x1,y1,x2,y2,…]`；Aitiy 用空格分隔的坐标对字符串 `"x1,y1 x2,y2 …"`。两者**语义相同**：坐标归一化到约 `0..100` 的范围，且刻意保留 `-5`、`105` 这类「越界」点作为出入笔信号。 |
| **动作（Action）** | 可执行的最小单位。MouseInc 写成 JSON 数组 `["族名","子动作","参数"]`，如 `["SendKeys","Ctrl+C"]`；Aitiy 写成一段 Lua 脚本，如 `aitiy.keyboard.send('Ctrl+C')`。 |
| **动作链（Actions）** | 一个手势绑定的动作**序列**。MouseInc 中是数组的数组，元素间固定间隔 100ms，**顺序执行**，前一个未完成不进入下一个。Aitiy 中由 Lua 脚本内部语句顺序体现。 |
| **搭配（Stick）** | 又称贴图：把一张静态图钉在屏幕上。官方交互（MouseInc）：左键拖动、Esc/右键退出、拖到窗口边缘可缩放、双击/空格/回车在缩略图与原图间切换、滚轮调透明度、`Ctrl+C` 复制、`Ctrl+S` 保存、`Ctrl+O` OCR、`Ctrl+W` 关闭、`w/a/s/d` 每次移动 10px、方向键每次 1px。 |
| **画中画（PiP / crop_lock）** | 把**目标窗口区域**钉在屏幕上并持续跟踪其内容。MouseInc 叫 PiP；Aitiy 改名为 `crop_lock`（`aitiy.window.crop_lock()` / `clear_crop_locks()`）。注意社区有「Aitiy 没有画中画」的说法，**该说法不准确**，是改名所致。 |
| **滚轮快切（WheelSwitch）** | 按住右键的同时滚动滚轮，触发 `WheelSwitchUp` / `WheelSwitchDown` 两个特殊 Sign（它们是伪手势，不是画出来的形状）。 |
| **边缘滚动（WheelEdge）** | 滚轮在屏幕四条边滚动/按下时触发，四个边各自独立配置 Up/Down/Press 三组动作。 |
| **触发角（HotCorner）** | 鼠标移到屏幕四个角触发，四角各自独立配置。 |
| **应用规则** | 按进程名（可多进程共用一组）覆盖手势表：`MatchGlobal`（全局）/ `MatchCustom`（含 `IgnoreGlobal` 决定是否屏蔽全局）/ `Excludes`（排除列表，忽略大小写）。 |
| **变量展开** | MouseInc 动作参数中的内置变量：`%appdir%`、`%clipboard%`、`%tempstr%`、`%filepath%`、`%filename%`、`%pid%`、`%port%`。以 `http` 开头的参数自动 urlencode。 |
| **Infinite Mouse** | Aitiy 的跨机鼠标：局域网内多台 Win/Mac 配对，按物理布局排列，鼠标推向屏幕边缘即切换主机，支持剪贴板同步与文件拖放。 |
| **选中工具栏（selection_toolbar）** | Aitiy 新功能：选中文本后弹出可配置按钮栏，每个按钮绑定一个动作与图标；也支持双击 Ctrl+C 触发（`double_ctrl_c_enabled`）。 |
| **寻找鼠标（find_mouse）** | Aitiy 新功能：双击 Ctrl 或摇动鼠标时高亮指针。可配颜色、尺寸、遮罩强度、时长。 |

## Important relationships

### 本机两个参考程序

| | MouseInc | Aitiy |
|---|---|---|
| 版本 | **2.13.4** | **1.0.5** |
| 主程序 | `MouseInc 便携目录\MouseInc.exe` | `Aitiy 安装目录\Aitiy.exe` |
| 大小 | 1,243,648 B（约 1.19 MiB） | 27,579,904 B（约 26.3 MiB） |
| 构建时间 | 2022-09-10 | 2026-09-30 |
| SHA256 | `2007971F7C44DBEAA0C6D2F06933A1F0B8F0E228A67257149658F40E2B8B6EA8` | `7B3A8A1E4FE7EC6AB7DD84303B904BC9D710974718F655FDCAEB8D0257C91E42` |
| 版本资源 | Product `MouseInc` / Company `shuax.com` | Product `Aitiy` / Company `Aitiy Team` / Desc `Aitiy - Make Your Mouse Do More` |
| 配置位置 | 程序同目录 `MouseInc.json`（UTF-8，不支持注释） | Aitiy 用户数据目录（**不在** `Aitiy 安装目录\Aitiy.exe` 旁边） |
| 配置体积 | 18,296 B（用户实际在用的活配置） | `config.json` 30,373 B + `language.json` 573,809 B + `stats.json` + `infinite_mouse.json` |

Aitiy 当前正在本机运行（PID 12908，工作集约 35 MB）。

### 二进制指纹（实测 PE 导入表 + 字符串，非推测）

**MouseInc.exe 2.13.4** 导入：`advapi32 / atlthunk / comdlg32 / dwmapi / gdi32 / gdiplus / imm32 / kernel32 / msvcrt / ole32 / oleaut32 / shell32 / shlwapi / user32 / version / wininet / winmm / ws2_32`。

由此**确认**的技术事实：
- **32 位（PE32 / x86, I386）**【实测】。对照 Aitiy 是 **64 位（PE32+ / x64）**【实测】。这条对「轻量」目标的设定有直接影响——**不能拿 MouseInc 的 6 MB 当本工程 x64 构建的验收基线**，详见「作者的设计取向与稳定性来源」。
- `atlthunk.dll` ⇒ 使用 ATL/WTL。与上游 2015 年完整源码仓库（C++ + WTL + VS 工程）的技术路线一致。
- 无 `vcruntime140.dll` / `msvcp140.dll` 导入 ⇒ CRT 静态链接，即官方宣称的「单执行文件，无运行库依赖」。
- `gdiplus.dll` ⇒ 截图绘制/贴图渲染用 GDI+。
- 出现 `DwmRegisterThumbnail` 字符串 ⇒ **画中画用 DWM 缩略图实现**（此前社区推测是 PrintWindow/BitBlt，推测有误）。
- `wininet` ⇒ HTTP（更新检查、OCR 上传）；`ws2_32` ⇒ 本地设置通道；`imm32` ⇒ 输入法状态显示。
- 无 `WebView2` 字符串 ⇒ WebView2 为运行时动态加载（佐证：`%TEMP%\MouseIncWebView2` 目录存在，程序目录带离线界面包 `MouseInc.Settings\`）。
- 无 `Lua` / `luaL_newstate` 字符串 ⇒ 后期版本未把 Lua 暴露给用户（2015 年 v2.1 有内置 Lua 引擎，属能力回退）。
- 无 UPX 特征（`MZ` 头原样）⇒ 未加壳，与更新日志「2.11.1 去除程序压缩」一致。

**MouseInc 设置界面（离线包）**：`MouseInc.Settings\` 是 webpack 构建的 Vue 产物（2022-09-06），`index.html` 以 `/mouseinc/` 为基路径加载 `js/app.6d3b6636.js`（1.5 MB）。设置界面通过 **WebSocket 连 `ws://127.0.0.1:<port>`** 与主程序通信，端口通过配置变量 `%port%` 暴露；官方也支持用外部浏览器打开在线版设置。主程序与前端之间的这套 WebSocket 协议是本工程**可以自行设计替代**的部分，无需照搬。

**Aitiy.exe 1.0.5** 导入：`advapi32 / api-ms-win-core-path / api-ms-win-core-synch / atlthunk / bcrypt / combase / coremessaging / d2d1 / d3d11 / d3d12 / d3d9 / d3dcompiler_43 / d3dcompiler_46 / d3dcompiler_47 / dbghelp / dcomp / dwmapi / dwrite / dxcore / dxgi / gdi32 / gdiplus / imm32 / kernel32 / msvcrt / mswsock / ole32 / oleacc / oleaut32 / opengl32 / setupapi / shell32 / shlwapi / user32 / winhttp / ws2_32`。

由此**确认**的技术事实：
- `d2d1` + `dcomp` + `dwrite` + `dxgi` ⇒ **自研原生 UI 用 Direct2D + DirectComposition + DirectWrite**，与官方 v1.0.0 表述「in-house native UI engine with no WebView dependency」相符。无 `WebView2` 字符串，确认已弃用 WebView。
- `onnxruntime`（28 处）、`paddle`、`PP-OCR` ⇒ **OCR 用内嵌 ONNX Runtime + PP-OCR 系列模型本地推理**，与 v1.0.4 变更日志「文本识别升级到 PP-OCRv6」相符。进一步 PE 取证【实测】：`.rsrc` 只有 85 KB（全是指标图标），**模型与语言包以字节数组内嵌在 `.rdata`**；在 `.rdata` 15.6–22.0 MB 处有约 **6.4 MB 的 protobuf 模型区**，含 `p2o.pd_op.conv2d.*`、`hardsigmoid` 等标识符（`p2o`/`pd_op` 是 **paddle2onnx** 的节点名前缀）。6.4 MB 与公开的「PP-OCRv6 tiny det(1.88 MB) + tiny rec(4.53 MB) ≈ 6.4 MB」几乎精确吻合，说明**它只内嵌 tiny 模型、更大的按需下载**（这部分克制值得学）。`.text` 15.6 MB 的大头最可能是静态链接的 ONNX Runtime（公开 ORT DLL 体积 9.8–14.5 MB）【推断】。
- `lua_` ⇒ Lua 引擎静态链接进主程序（动作引擎）。
- `oleacc` ⇒ 用 UI Automation 取选中文本（`aitiy.selection.get_text()` 的底层）。
- `atlthunk` ⇒ 同样使用 ATL/WTL 系窗口基础设施。
- `bcrypt` + `mswsock` + `winhttp` ⇒ 设备配对加密与局域网/网络通信（Infinite Mouse）。
- 同目录存在 Aitiy 同目录的 `ANGLE-Licenses`，且导入 `opengl32` + 三个 `d3dcompiler_*` ⇒ 渲染栈里包含 ANGLE（OpenGL ES → D3D 转换）。**推测**其 UI 渲染管线混合了 D2D 与 GL 系路径。
- 无 `thorvg` 字符串 ⇒ 上游调研中「Aitiy 的 UI 可能基于 thorvg」的推测**未被证实**，此处以实测为准。

### 手势识别数据模型（复现的核心）

两者的手势模板是**同一套归一化坐标**，只是序列化形式不同：

```
MouseInc:  {"Sign":"DOWN-RIGHT","Data":[0,0,0,100,100,100]}
Aitiy:     {"shape":"DOWN-RIGHT","path":"0,0 0,100 100,100"}
```

结论：**识别算法是归一化模板轨迹匹配（whole-path template matching），不是几何特征判别**。这解释了官方为何提供 `Sensitive`（灵敏度）参数，也解释了用户反馈的「斜向轨迹被识别成转弯轨迹」类误差。

MouseInc 内置 **36 个手势模板**（实测 `Gestures` 数组计数），涵盖：四方向、四条斜向（`\ UP` `\ DOWN` `/ UP` `/ DOWN`）、四组往返（`UP-DOWN`、`LEFT-RIGHT` 等）、八组双段折线（`UP-LEFT`、`RIGHT-DOWN` 等）、`UP-RIGHT-UP`、`DOWN-RIGHT-DOWN`、`SQUARE`、`SQUARE 2`，以及字母数字形 `M W C O P R B h N S Z 3`。Aitiy 的 `gesture_list` 与之一一对应（同 36 条）。

**识别算法已由作者规格给定**（`temp/input/mouseinc-author-spec.md` 第五节），实现时照此写，不要另创算法：

1. 轨迹是有序二维点序列。
2. 去除连续重复点。
3. 点数不足、总长度接近零时拒绝识别。
4. 计算累计路径长度。
5. 按路径长度**等距重采样为 100 个线段（101 个点）**。
6. 用 `atan2` 计算每段方向角，得到 **100 维特征**。
7. 对输入与模板计算对应角度的**最短环形差**，范围 0～π。
8. 计算平均角度差。
9. 相似度 `score = 100 × (1 − meanAngularDifference / π)`。
10. 通过阈值 `threshold = 60 + 30 × sensitivity / 100`。
11. 选取达到阈值的**最高分**模板，否则无匹配。
12. 同分采用**确定性规则**；诊断模式可查看候选分数。

设计意图与约束：降低位置、整体大小、绘制速度的影响，但**保留绘制方向差异**（上下、左右不能互相混淆）。必须处理空轨迹、单点、零长度线段、浮点误差、采样数量、角度环绕、畸形模板、过长轨迹。**相似度是评分，不得称为统计概率。**

MouseInc `MouseGesture` 配置项与用户当前取值：`StartDistance 5`（起手阈值）、`Timeout 1000`（等待时间）、`Sensitive 100`、`Offset 150`、`TraceWidth 3`、`FontSize 26`、`DrawColor #E47542`、`FailColor #CAD0D3`、`DrawTrace false`、`DrawResult false`、`RandColor false`、`RestoreEvent false`、`AddMode false`、`WheelSwitch true`。

### 触发方式（5 类 + 1 伪手势）

鼠标手势 / 滚轮快切 / 边缘滚动 / 触发角 / 全局热键，外加复制增强（0.5 秒内连按两次 `Ctrl+C` 弹出快捷菜单，二级菜单，选中文字长度上限 100k）。

### 动作族（MouseInc，来自实测配置 + 官方 `action.md`）

`Window`（Maximize/Minimize/Top/Center/Close/CloseSimilar/HideTray/ShowTray/ToggleTray）、`Internal`（Exit/Pause/Icon/ClipboardMenu/Exclude/Reload/Config/Settings/Delay/Print/ShowTips/tempstr）、`SendKeys`、`SendKeyDown`/`SendKeyUp`、`Activate`/`SendClick`/`MouseMove`、`SetClipboard`、`Execute`/`Execute2`、`Screenshot`/`Snapshot`/`ScreenshotHQ`/`GetClipboard`、`Algorithm`（b64/url/md5/sha1/sha256/qrcode/CyberChef）、`Explorer`（select/recyclebin/turnoffmonitor/ToggleDesktopIcons）、`SetBrightness`、`PostMessage`、`RegSet`。截图动作的子命令统一为 `ToClipboard` / `ToFile` / `Stick` / `PiP` / `OCR`。

### 动作模型（Aitiy，来自实测 `config.json`）

默认动作库共 **80+ 个命名动作**，分 10 组：`editing` / `browser` / `window` / `capture` / `text` / `system` / `tools` / `aitiy` / `reminders` / `custom`。每条是 `{group, name, script}`，`script` 是 Lua 代码。

Lua API 命名空间：`aitiy.shell` / `keyboard` / `mouse` / `clipboard` / `selection` / `text` / `i18n` / `window` / `screen` / `image` / `system` / `app` / `config` / `state` / `ui` + `sleep`。命名约定 `aitiy.domain.verb_object`，`get`=读、`set`=赋值、`toggle`=切换、`adjust`=相对调整、`is`=返回布尔。

脚本可见的顶层绑定：`context`（触发时快照，含 `process_path`、`window_handle`、`process_name` 等）与 `aitiy`。

Aitiy 配置的顶层键：`actions`、`gesture`、`gesture_list`、`match_global`、`match_custom`、`hotkey_list`、`screen_edge`（`edges` + `corners`）、`schedule`、`selection_toolbar`、`key_echo`、`find_mouse`、`input_stats`、`infinite_mouse`、`ocr`、`global`、`general`、`telemetry`。命名风格为 `snake_case`（对比 MouseInc 的 `PascalCase`）。

### Aitiy 的定时任务（MouseInc 没有的能力）

`schedule.tasks[]` 形如 `{id, name, action, trigger, enabled}`，实测的 trigger 语法有：`cycle 25m,5m`、`at 18:00`、`every 50m`、`every 1h at :30`。这是自建的轻量定时表达式，不是 cron。

### 本工程配置系统的既定事实（T-0003 已实现并实测）

- **顶层键**（与作者规格「十四、设置与配置」一致）：`SchemaVersion`、`General`、`MouseGesture`、`Gestures`、`MatchGlobal`、`MatchCustom`、`Excludes`、`Hotkeys`、`WheelEdge`、`HotCorner`、`ClipboardMenu`。当前 `SchemaVersion = 1`。
- **配置文件位置**：便携模式用 exe 同目录的 `mouselnk-plus.json`（判定见「待确认」第 5 条已解决项），否则用 `%APPDATA%\mouselnk-plus\config.json`。
- **默认配置内嵌在 .rc 里**（`IDR_DEFAULT_CONFIG`，RCDATA 原样嵌入，UTF-8 不被转码）——作者规格明确要求「使用 .rc 文件嵌入图标、默认 JSON、图片和 WAV 音效」。
- **未知字段一律拒绝并报错**（不是静默丢弃）：`General`、`MouseGesture`、`Gestures`/`MatchGlobal`/`MatchCustom` 的条目都按白名单校验字段。动作对象是唯一例外——它**只强约束 `Type`**，其余参数整体保留，因为动作参数集随任务扩展（`Execute` 的提权/等待、`Screenshot` 的输出方式等），建模字段会导致每加一个参数都要改结构体。
- **尚未实现行为的四个区块**（`Hotkeys`/`WheelEdge`/`HotCorner`/`ClipboardMenu`）先按**不透明 JSON 原样保留**：字段已在文件里、读写往返不丢数据，等各自任务落地时再建结构体。这是有意的取舍——不替未实现的功能发明结构。
- **写入是原子的**：先写同目录 `.tmp` 再 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`，断电或崩溃不会留下半截配置。
- **损坏配置备份命名**：`config.json.corrupt-YYYYMMDD-HHMMSS`，原文件改名留档后回落默认配置，并弹窗告知用户备份文件名。
- **重载语义**：读入候选 → 解析 → 语义验证 → 通过才替换内存快照。因此重载失败时**当前配置仍然有效**，不会出现半成品。读取走 `Snapshot()` 取完整副本（配置很小，复制比引用计数更难出错）。
- **语义验证的范围**（已实现）：SchemaVersion 上下界、手势参数范围（`StartDistance ≥ 0`、`TimeoutMs > 0`、`Sensitivity ∈ [0,100]`、`TraceWidth ≥ 1`、`FontSize ≥ 1`）、模板 Id 非空且不重复且点数 ≥ 2 且坐标有限、绑定引用的模板必须存在（`WheelSwitchUp`/`WheelSwitchDown` 作为伪手势例外）、动作必须有 `Type`、应用规则必须有 `Programs`、`Excludes` 无空串。
- **文件读取容忍 UTF-8 BOM**（部分编辑器会加，不去掉会导致解析失败）。

### 技术选型调研结论（2026-10-09，完整对比见 `docs/execution-plan.md` 第 2 节）

以下**事实**是选型依据，无论最终怎么选都成立：

- **Windows 自带 OCR 可用**：`Windows.Media.Ocr` 的 `OcrEngine` 在本机**以普通非 MSIX 进程**成功创建并识别（1000×140 图 31 ms，中英数混排全对）。MS 文档把 `OcrEngine` 列在「需要 package identity」清单里，所以这是**文档层面「不受支持」但实际可用**——必须按「探测可用则用、否则降级」实现，不能假设一定成功。语言数据在系统侧（本机 zh-cn 为 2.3 MB），**主程序零字节增量**。ONNX Runtime + PP-OCR 路线约 24 MB（ORT 17.6 MB + tiny det/rec ≈ 6.2 MB），正是 Aitiy 空闲内存 130–176 MB 的来源。
- **BCrypt/CNG 可以替代作者规格点名的 LibTomCrypt + LibTomMath**：BCrypt 覆盖 MD5/SHA-1/SHA-256（Vista+），零字节增量、零许可证登记；LibTomMath 本项目根本不需要（其官方 README 称 MPI 仅在构建 binaries 时需要）。更新包验签可用 CNG 验签或 `WinVerifyTrust`。
- **miniz 不可替代**（只要必须读任意 `.zip`）：Windows 的 Compression API 只有 MSZIP/XPRESS/XPRESS_HUFF/LZMS，**没有 DEFLATE**，解不了标准 zip；ntdll 里的 raw DEFLATE/ZLIB 仅 Win11 24H2+ 且未公开。**除非**把可选组件包格式改由我方定义（MSZIP/LZMS），那样可以完全不用 miniz。
- **原生 Windows 的单文件程序没有可用的自动更新轮子**：唯一原生候选 WinSparkle 单 DLL 就是 2.2–2.8 MB（大于 MouseInc 整包 1.19 MiB）且需多带一个文件；Velopack / Squirrel.Windows / Google Omaha 都以「安装器 + 目录布局」为前提，与单文件 portable **架构层面互斥**（Squirrel 已停滞、Omaha 已归档）。
- **崩溃上报同理没有合适轮子**：Crashpad 必须随包一个 handler 子进程且构建不走 CMake；sentry-native 默认指向云端且同样要额外进程；WER LocalDumps 需管理员写 HKLM，便携场景不可用。可用系统自带的 dbghelp `MiniDumpWriteDump`（延迟加载，空闲零开销）。
- **WebView2 两个常见误解要纠正**：① 对非 UWP 应用，**默认 user data 目录就在 exe 旁边**（`{exe文件名}.WebView2`），不是系统目录——真正的硬伤是 exe 目录不可写时会失败、以及会在程序旁留一个体积不小的缓存目录；解法是显式传 `userDataFolder`（便携模式用 exe 同目录 + 可写性探测，回落 `%LOCALAPPDATA%`），注意环境变量 `WEBVIEW2_USER_DATA_FOLDER` 与注册表策略会**覆盖**程序传入值。② **不需要 Mongoose、不需要起回环服务**：用 `SetVirtualHostNameToFolderMapping` 把本地 `ui/` 目录映射为虚拟域名即可离线加载。另外 Fixed Version 运行时对便携程序不可接受（包体 +250 MB、需改 ACL、不能从网络路径运行），应走 Evergreen + 启动探测 + 明确告知。
- **`WebView2LoaderStatic.lib`（静态 loader）要在 CMake 下手动集成**（解 nupkg 取 `build/native/x64/WebView2LoaderStatic.lib`，JUCE/wxWidgets/Tauri 都走这条路，官方给的是 MSBuild 属性，我们用不上）。它是保住单文件的关键，**但与静态 CRT `/MT` 的兼容性无官方说明，必须真机构建验证**。

## 作者的设计取向与稳定性来源（调研结论）

四份专题调研（`temp/research/mouseinc-deep-dive.md`、`aitiy-deep-dive.md`、`community-voice.md`、`stability-and-design.md`）与综述（`design-thinking-synthesis.md`）得出的结论。**这些解释了我们为什么要守住「轻量」，不是背景故事。**

### MouseInc 的总纲

> **「专注小巧实用免费软件」**

来源：作者 2012 年博客副标题（https://web.archive.org/web/20120206181910/http://www.shuax.com/archives/MouseInc.html ）。这五个词互相咬合：小 → 少依赖少体积；实用 → 不做花哨功能；免费 → 没有增长指标，也就不需要为留存堆功能。

他对「不做某件事」的唯一一处直接原话（有人要求附 PDF 手册）：「**软件才 200k，我不可能放一个 1M 的手册在里面。**」——**体积在他的决策函数里是硬约束，能直接否掉一个功能。**

便携在他看来不是功能：「直接拷走就是了，随你在哪儿用」。因此到 2.13.4 都没有配置导入导出 UI。

### 四条「设计换稳定」

1. **静态 CRT + 单文件 + 无运行库依赖** ⇒ 消掉「用户机器缺 VC++ 运行库导致启动失败」整类故障。
2. **配置只有一个 JSON，且「坏了就删」是官方推荐做法** ⇒ 把「配置损坏」降级成用户自己动手就能解决的操作（缺文件即重建），用一个反常识的默认行为替代版本迁移逻辑。
3. **动作异步 + 多线程执行** ⇒ 把「卡死」挡在主线程外（更新日志 2.4.3 / 2.8.3）。
4. **默认获取管理员权限** ⇒ 用「每次开机一个 UAC 弹窗」换掉「不再有人问为什么右键没反应」（作者原话：「小白太多了，天天要问一百遍为什么点右键没反应」）。

第 2、4 条是「**支持成本**」驱动的选择，不是技术选择——这一点对本工程有直接借鉴价值。

### 稳定性的真实来源是「范围稳定」

作者几乎从不承诺「下个版本修复」，对搞不定的场景直接承认并排除：4K 卡顿「没有 4k 环境，暂时解决不了」、Win7 假死「暂时无解」、QQ/微信快速粘贴「我也没办法」、Office SendKeys 失效「我也不知道为什么」、某些程序失灵「可能不兼容吧，建议换别的」。

**结论：MouseInc 的稳定性口碑来自「把不稳定场景明确排除在支持范围外」，而不是把所有场景都修好。** 本工程不采用这个策略——规格第十七节要求交付「已实现功能与实际验证范围」，把边界写清楚是交付物的一部分，但不能当「不修」的借口。

### 运行时资源对照（实测）

| 指标（空闲态，60 秒 4 次采样） | MouseInc 2.13.4（32 位） | Aitiy 1.0.5（64 位） |
|---|---|---|
| 工作集 | **6.0 MB，4 次采样完全相同** | 25.2 → 30.9 MB，**持续上升** |
| 私有提交（Private Bytes） | **9.9 MB，完全静止** | **175.4 → 130.4 → 176.4 → 161.1 MB，摆动 46 MB** |
| 线程数 | **21，静止** | 77–78 |
| 句柄数 | 546，静止 | 656 → 661 |
| GDI / USER 对象 | 17 / 42 | 10 / 24 |

**最本质的差别不是某个绝对数字，而是「空闲时是否仍在持续分配」。** 一个空闲的常驻工具应该是四项指标静止的形态。

### 两条口径纪律（必须遵守）

1. **任何内存数字都必须写明口径**（`WorkingSet` 还是 `PrivateBytes`、是否用过 OCR/截图、刚启动还是长期运行）。社区报的 Aitiy「260 MB」与本机测到的私有提交 130–176 MB 同一量级，本机测到的工作集 25–31 MB 与更早记的 35 MB 同一量级——**两边都是真的，只是量的不是同一个东西**。写错口径的数字会在社区被反复引用成「矛盾数据」。
2. **GDI 对象数不能当「好坏」指标**：MouseInc 反而更多（17 vs 10），因为它是 GDI+ 路径而 Aitiy 是 Direct2D。它是渲染路径选择的产物，不是泄漏证据。

### 与 MouseInc 的取舍不同之处（本工程主动选择）

- **不能拿 MouseInc 的 6 MB 当验收基线**：它是 32 位，我们是 x64（规格定的）。静态 CRT 下 32 位代码通常更紧凑（指针宽度减半、无 `.pdata`、地址空间上限本身是一种自律），同量级工作量的内存必然更高。**阈值必须以自身空壳基线为分母。**
- **不采用「范围稳定」策略**（理由见上）。
- **不重走 Aitiy 的重量路线**：主程序不背推理引擎、不背浏览器内核、不背跨机栈。

### 可执行清单

`temp/research/stability-and-design.md` 第 5 节给出带编号与依据的完整清单：H（钩子存活与恢复，11 条）/ M（内存与句柄泄漏防护，7 条）/ R（渲染路径，5 条）/ D（依赖引入，6 条）/ L（生命周期，7 条）/ P（高 DPI 与多屏，6 条）/ S（安全软件与全屏与高权限，8 条）/ O（崩溃可观测性，6 条）/ U（轻量自我约束，5 条）。

其中三条必须在阶段 1 就做对，否则后面返工：

- **H-1**：钩子必须装在专用消息泵线程上，**所有安装/重装/卸载都必须 post 回该线程**；从工作线程直接调 `SetWindowsHookEx` 会装上一个永远收不到投递的钩子。
- **H-4**：用 **Raw Input（`RIDEV_INPUTSINK`）做独立投递路径的存活看门狗**——「Raw Input 在动 + 钩子静默 = 钩子被丢弃」，「两者都静默 = 只是没人动鼠标」。这是唯一有证据的判定方式（系统不提供任何「钩子是否被移除」的查询）。
- **L-3**：退出必须保证「无卡键、无残留浮层、无残留托盘图标」，包括释放 `SendInput` 期间按下的修饰键。

清单中所有标注为【推断】的条目（特别是 L-1/L-3 生命周期顺序与残留、P-3 负坐标、O-3/O-5 可观测性）**是本工程实现时必须在真机上验证的假设，不是已验证结论**。

## Hard constraints

1. **法律/来源边界**：不得复制、反编译、反汇编参考程序；名称、图标、界面、服务地址全部自建（作者明确要求）。
2. **轻量与便携是产品定义的一部分**：用户选择留在 MouseInc 的首要原因就是它「轻量、稳定」。MouseInc 2.13.4 主程序 1.19 MiB；Aitiy 主程序 26.3 MiB 且社区实测常驻内存约 260 MB，是被最集中批评的点。本工程必须守住「单文件、无运行库依赖、配置随程序目录释放」这条线。
3. **工具链**：C++20 + MSVC v143 + Windows SDK（作者指定）。本机原本没有，2026-10-08 开始安装 VS2022 Build Tools；工程系统用 CMake，生成器 `Visual Studio 17 2022` x64。
4. **权限模型**：Vista 以上存在 UIPI，低完整性级别进程无法操作高完整性级别窗口（例：任务管理器在前台时普通权限的程序无法正常处理右键）。MouseInc 的官方解法是「强烈推荐以管理员权限运行」。Aitiy 默认 `startup_admin: true`。
5. **配置 schema 采用作者规格给定的新格式**（见 ADR-0002），它与 MouseInc 的 `MouseInc.json` **不兼容**——字段名、嵌套层次、动作表示三者都不同。既有 `MouseInc.json` 只能作为**一次性导入源**，不是运行时格式。不要为了「能直接读 MouseInc.json」而把内部 schema 改成 MouseInc 的形状。
6. **必须屏蔽右键才能做手势**：这一需求决定了钩子方案只能是 `WH_MOUSE_LL`（Raw Input 只能旁路监听、无法吞掉事件）。两者是互补关系，不是替代关系。
7. **依赖引入纪律**：不要为尚未开发的功能提前引入全部依赖；先集成当前阶段需要的库，后续按模块增加，并记录第三方许可证。
8. **阶段纪律**：作者规格要求「每个阶段都应保持工程可编译、程序可运行」，完成一个阶段后更新说明、验证结果与剩余工作，再进入下一阶段。不要把多个阶段混成一次大改动。
9. **「轻量」必须做成可失败的检查**，不能只是口号：主程序体积上限、空闲工作集上限、空闲私有提交上限、线程数上限、依赖 DLL 数 = 0，进 CI。阈值**以本工程自身的空壳基线为分母**，不以 MouseInc 的绝对值为分母（它是 32 位，我们是 x64）。依据见「作者的设计取向与稳定性来源」与 `stability-and-design.md` 的 U-1。
10. **不把 ONNX Runtime / 任何推理引擎编进主程序**。OCR 做成「用户显式开启 + 按需下载」的可选组件，主程序在没有它时功能完整可降级。依据：Aitiy 的 26.3 MB 与空闲私有提交抖动最可能的主因就是内嵌推理栈（`.rdata` 内约 6.4 MB 的 PP-OCR tiny 模型区 + `.text` 内约 15.6 MB 疑似静态链接的 ONNX Runtime）。
11. **新功能进入实现前必须先回答「它在空闲态占多少内存和线程、用什么方式实现」**——这是**门槛，不是否决权**。owner 已明确要集成 Aitiy 特性，因此不能用「重」当理由一律拒掉；正确的做法是给出实现方案与开销预算，再决定用哪种方式落地（例：OCR 走按需下载的可选组件，而不是编进主程序）。依据：Aitiy 的定时任务、输入统计、寻找鼠标、选中工具栏都是新增常驻负担，其中一部分完全可以用极低开销实现。

## Known failure modes

复现时必须主动规避的、有实测来源的失效模式。

| 失效模式 | 具体表现与成因 | 来源 |
|---|---|---|
| **低级钩子被系统静默移除（最致命）** | `WH_MOUSE_LL` 回调执行时间超过 `LowLevelHooksTimeout`（默认 300ms）时，Windows 会移除该钩子**且不通知任何人**：`HHOOK` 仍然非 NULL，只是再也不触发。系统从睡眠恢复时最容易触发，高负载下也会发生。这是「手势软件莫名其妙失效、重启就好」的根因。 | [Mouser PR #264](https://github.com/TomBadash/Mouser/pull/264)、[localflow PLAN-INPUT-CONTINUITY](https://github.com/Joshua-Anojulu/localflow/blob/master/PLAN-INPUT-CONTINUITY.md) |
| **现代待机（Modern Standby）后钩子失效** | `WM_DEVICECHANGE` 在 Modern Standby 下**不一定触发**（USB 保持枚举）。恢复检测需要 `WM_POWERBROADCAST` + `GUID_CONSOLE_DISPLAY_STATE` + 解锁事件三路汇聚，并按 0.5s / 3s / 10s 阶梯重试、5s 去重。 | 同上 |
| **回调线程不是「随便一个线程」** | 低级钩子被投递到**安装钩子的那个线程**的消息队列，该线程必须持续 pump 消息。重装钩子必须 post 回钩子线程，从工作线程直接调 `SetWindowsHookEx` 会装上一个永远收不到投递的钩子。 | 同上 |
| **返回值语义是全局的** | `WH_MOUSE_LL` 回调返回**非 0 会让操作系统在整个系统范围内丢弃该事件**，不是仅对本进程生效。只想观察时务必 `return CallNextHookEx(...)`。 | [OptiScaler #1124](https://github.com/optiscaler/OptiScaler/issues/1124)、[MSDN LowLevelMouseProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelmouseproc) |
| **多个钩子互相干扰** | 未调用 `CallNextHookEx` 会导致其它同样装了 `WH_MOUSE_LL` 的程序收不到通知。MouseInc 官方 FAQ 也警告「不要和同类软件一起使用」。 | MSDN 同上、[MouseInc FAQ](https://docs.shuax.com/MouseInc/) |
| **高 DPI 坐标偏移** | 高 DPI 环境下若当前窗口不感知 DPI，画出的轨迹会偏移，甚至让命令作用到非起始位置的窗口上。MouseInc 长期存在此问题（用户实测反馈）。自研时必须全程使用物理像素坐标。 | [小众软件论坛 t/87071](https://meta.appinn.net/t/topic/87071) |
| **`RIDEV_NOLEGACY` 极其危险** | 使用 Raw Input 做旁路探针时，该标志会彻底关闭该设备类的传统消息（鼠标的 `WM_MOUSEMOVE` / `WM_LBUTTONDOWN` 全部消失），官方标注为对标准应用「exceptionally dangerous」。 | [MSDN About Raw Input](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input) |
| **`LowLevelHooksTimeout` 注册表项可能不存在** | 该值有 300ms 默认值但注册表里可能查不到，不能靠读注册表来判断阈值。 | localflow 同上 |
| **Win11 下 DWM acrylic 对全屏窗口失效** | Win11 的 DWM 会禁用全屏窗口的 acrylic 模糊，色调塌陷为不透明填充（可能变纯黑），而 **API 仍返回成功**。MouseInc 的 `ScreenshotHQ`「保留 Win7/Win10 毛玻璃」这一能力在 Win11 上原理性失效。 | [dev.to](https://dev.to/darshan_rathod/i-wanted-a-better-app-launcher-for-windows-so-i-built-one-1fhn) |
| **品牌与兼容风险** | 参考程序与安全软件长期冲突（MouseInc 官方 FAQ：杀软报毒、被提示监控键盘、360 会导致无法正常点击右键、开机启动失效），且都靠代码签名/白名单缓解。本工程作为新软件会再次面对同样问题。 | [MouseInc FAQ](https://docs.shuax.com/MouseInc/) |

## 本机开发环境（2026-10-08 实测并验收）

| 组件 | 状态 |
|---|---|
| Visual Studio Build Tools 2022 | 17.14.41，装在 `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` |
| MSVC 工具集 | 14.44.35207（v143），`...\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe` |
| Windows SDK | 10.0.26100.7705，`C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0` |
| vcvars | `...\VC\Auxiliary\Build\vcvars64.bat` |
| **ATL** | `Microsoft.VisualStudio.Component.VC.ATL`，装后在 `...\VC\Tools\MSVC\14.44.35207\atlmfc\`。**WTL 的硬依赖**（`atlwin.h`/`atlbase.h` 属于 ATL 而非 WTL）。首次安装的 VCTools 工作负载**没带**它，已于 2026-10-08 补装 |
| WTL | 10.0（`_WTL_VER 0x1000`），vendored 在 `third_party/wtl/Include/`，MS-PL，见 `THIRD-PARTY-NOTICES.md` |
| CMake | 4.3.3 |
| 其它 | Git 2.54.0.windows.1、Node v24.14.0、Ninja 与 MinGW-w64 g++ 16.1.0（本机另装，本路线不使用） |

验收方式与结果：`temp/scripts/vscheck/verify-toolchain.bat` 编译 `hello.cpp`（`/std:c++20 /Zc:__cplusplus /MT /O1 /EHsc /W4`）→ `VCVARS_ERRORLEVEL=0`、`CL_EXIT=0`、`__cplusplus=202002`（C++20 生效）、产物 140,800 字节、导入表只有 `kernel32.dll`（确认静态 CRT，无 `vcruntime140` / `msvcp140` 依赖）。

三条踩坑记录（复现成本高，不要再踩）：

1. **VS 安装器 `--quiet` 必须提权**，否则报 `Exit Code 5007`（"Commands with --quiet or --passive should be run elevated from the beginning"）。本机 UAC 为 `ConsentPromptBehaviorAdmin = 0`（管理员静默提权、不弹窗），因此 `Start-Process -Verb RunAs` 可以无人工干预地提权；普通 shell 是 `IsAdmin=False`。
2. **winget 可能报「已成功安装」而实际空跑**。Windows SDK 最初只落地了 `Catalogs` / `Licenses` / `Redist`，`Include`、`Lib`、`bin` 全缺，但注册表已有卸载记录（ARP）；此后所有安装器（winget 重装、VS 安装器 modify）都据此判定「已安装」而直接跳过，表现是几十秒内退出、报成功、文件依旧缺失。解法：先**提权卸载**这条幽灵 ARP 记录（`UninstallString` 指向 `C:\ProgramData\Package Cache\{c0b93b80-…}\winsdksetup.exe`），ARP 记录清零后再提权重装，SDK 才真正落地。
3. **MSVC 的 `__cplusplus` 默认是 `199711L`**，即使已在 C++20 模式下。要让宏反映真实标准版本必须加 `/Zc:__cplusplus`，否则源码里所有 `#if __cplusplus >= 202002L` 分支都会被跳过。
4. **本机 MSBuild 无法构建任何 C++ 工程（机器级缺陷，未修复）**。现象：`error MSB8036: 找不到 Windows SDK 版本 10.0.26100.0`，连最小 `.vcxproj` 也一样。诊断结论：该检查要求 `$(WindowsSdkDir)` 指向 SDK，而 MSBuild 求值后 `WindowsSdkDir` 变成**单个反斜杠**（`\`）、`WindowsSDKVersion` 为空，所以 `Microsoft.Cpp.WindowsSDK.targets` 里的 `Exists(...)` 全部失败；同一台机器上 vcvars64 的环境变量却完全正确（`WindowsSdkDir` 有值、`WindowsSDKVersion=10.0.26100.0\`、INCLUDE/LIB 正确），`cl` 直接调用也能编译。根因是 VS 组件状态与实际 SDK 载荷不一致（VS `state.json` 声称 `Windows11SDK.26100` 已安装，但该载荷实际由独立安装器落地，重新 `modify --add` 也未能修复）。

   **影响**：`Visual Studio 17 2022` 生成器不可用；**Ninja 生成器 + cl 正常**，当前构建走这条路。这不影响交付物本身（CMake 工程不依赖生成器），但影响 VS/IDE 构建体验。已记为待修任务。
5. **`.rc` 必须显式按 UTF-8 读取**。rc.exe 默认用系统代码页（本机 GBK）解析无 BOM 的 `.rc`，中文注释被误解码后**会吞掉紧随其后的行**。实测后果：`resource.h` 里 `#define IDI_APP 101` 那行被注释吃掉，图标组的名字退化成字符串 `"IDI_APP"` 而不是序号 101，`LoadImage` 随即失败、托盘注册失败、程序启动即退出。修法：CMake 里 `set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /c65001 /nologo")`（已加）。**本工程要把默认配置 JSON 嵌进资源，里面有中文，这条是必需项。**
6. **PowerShell `Start-Process -ArgumentList` 传数组时不会为含空格的元素加引号**。`C:\Program Files (x86)\...` 会被截断成 `C:\Program`，VS 安装器随即报「找不到与以下参数匹配的已安装产品: installPath: C:\Program」。正确做法是把整条命令行写成**一个字符串并自带引号**。这正是本机 SDK 组件长期装不上的原因之一。
7. **无 BOM 的 UTF-8 脚本（.ps1 / .rc）在中文环境下都危险**：Windows PowerShell 5.1 同样按系统代码页读取无 BOM 的 `.ps1`，中文注释可使脚本解析失败（表现为变量静默为空、语句被跳过）。给这类脚本写中文注释时，要么存成带 BOM 的 UTF-8，要么注释用 ASCII。

## 本机取证清单（可复现的证据来源）

| 材料 | 路径 | 用途 |
|---|---|---|
| MouseInc 主程序 | `MouseInc 便携目录\MouseInc.exe` | 行为参照；PE 导入表取证 |
| MouseInc 活配置 | 同目录 `MouseInc.json` | 完整配置 schema + 用户真实手势/规则/热键表 |
| MouseInc 离线设置界面 | 同目录 `MouseInc.Settings\`（`index.html` + `js/app.6d3b6636.js` + `js/chunk-8edd2e4e.js` + `fonts/`） | 设置界面结构、WebSocket 通道（`ws://127.0.0.1:`）、UI 字段全集 |
| Aitiy 主程序 | `Aitiy 安装目录\Aitiy.exe` | 行为参照；PE 导入表取证 |
| Aitiy 数据目录 | Aitiy 用户数据目录（`config.json` / `language.json` / `stats.json` / `infinite_mouse.json`） | 完整配置 schema、命名动作库、10 语言界面文案（`language.json` 573 KB，是**功能面的最全清单**）、Infinite Mouse 配对结构 |
| Aitiy 官方文档 | `https://aitiy.com/docs.html`、`https://aitiy.com/llms.txt` | Lua API 参考（53 个 API）、沙箱限制、便携模式规则 |
| MouseInc 官方手册 | `https://docs.shuax.com/MouseInc/`（源文件：`shuax/MouseInc.Settings` 的 `docs/docs/*.md`） | 最权威的功能规格文档 |
| 上游技术路线源码（MIT，2015 年 v2.1） | `https://github.com/sanan2015/MouseInc` | **C++ + WTL + 内嵌 Lua** 的早期完整实现；仅作架构参考 |

注意：`language.json` 与 `docs.shuax.com` 手册是**功能面枚举**的最佳来源——前者列出 Aitiy 全部界面文案，后者列出 MouseInc 全部动作族与配置项。做功能覆盖对照表时先查这两处，不要凭记忆。

定性证据（社区评论、版本史、逐版本问题、设计取向、稳定性清单）与原始抓取件在 `temp/research/`：综述 `design-thinking-synthesis.md`、专题 `mouseinc-deep-dive.md` / `aitiy-deep-dive.md` / `community-voice.md` / `stability-and-design.md`、原始归档 `raw/`。资源实测脚本在 `temp/scripts/pe-analysis/`。

## Durable decisions

- `docs/adr/0001-toolchain-and-build-system.md` —— 编译工具链与工程系统选型（MSVC v143 + CMake）。
- `docs/adr/0002-config-schema.md` —— 内部配置 schema 采用作者规格的新格式，MouseInc.json 仅作一次性导入源。
- `docs/adr/0003-dependency-strategy.md` —— 依赖策略：系统自带优先、最少第三方；本工程采用 MIT 许可证。

## 待确认

1. **正式产品名称、图标、界面风格、服务地址**：作者明确要求「使用自己的软件名称、图标、界面和服务地址」。`mouselnk-plus` 只是仓库/目录名，不可直接当产品名对外。
2. **OCR 引擎的最终选型**：**已确认（2026-10-09）** —— 主用 Windows 系统 OCR（`Windows.Media.Ocr`，探测可用则用、否则降级），高质量需求走按需下载的可选组件（PP-OCRv6 tiny，独立进程）。理由与对比见 `docs/execution-plan.md` 第 2.2 节。
3. **作者原文是否还有被截断的尾段**：owner 提供的消息在「不要只给架构、伪代码或空 TODO。」处被对话界面截断。该句读起来是「十七、验证与交付」的自然收尾，但若原文其后还有内容，需要补发。
4. **是否实现 MouseInc.json 一次性导入**：内部 schema 已按作者规格定下（ADR-0002），两者不兼容。既有 `MouseInc.json`（36 条模板 + 应用规则 + 边缘/触发角/热键表）可作为**一次性导入源**帮用户迁移，但这是可选项，未排期。
5. **便携模式的判定方式**：已解决 —— **exe 同目录存在名为 `mouselnk-plus.portable` 的标记文件即进入便携模式**，配置写到 exe 同目录的 `mouselnk-plus.json`；否则配置写到 `%APPDATA%\mouselnk-plus\config.json`。选择「显式标记文件」而不是「目录可写就便携」的原因：后者会让行为随安装位置漂移（装在 Program Files 与放在 U 盘表现不同），用户无法预期。已实测验证两种模式各自的落点。
6. **第三方依赖的许可证清单归属**：已解决 —— 清单在仓库根的 `THIRD-PARTY-NOTICES.md`，首个登记项是 WTL 10.0（MS-PL）。每引入一个依赖就往里加一行。
7. **要集成哪些 Aitiy 特性、排在哪个阶段**——**owner 已决定：先不做选择，先把 MouseInc 骨架复现出来再说**（2026-10-08），且集成排期定为「**骨架先稳，再叠加**」。因此本阶段（作者规格的五个阶段）**只做 MouseInc 那一半**，Aitiy 特性集成是第二阶段工程，待骨架完成后再由 owner 从下表选定；届时才在 `TODO.md` 开任务。

   候选与实现代价（来自 `design-thinking-synthesis.md` 第 7.4 节，已备好供届时决策）：

   | 候选 | 实现代价 | 备注 |
   |---|---|---|
   | 寻找鼠标（双击 Ctrl / 摇动鼠标高亮指针） | 低 | 一个高亮浮层 |
   | 输入统计（键盘/鼠标计数） | 低 | 计数器 + 落盘 |
   | 定时任务（`every 50m` / `at 18:00` / `cycle 25m,5m`） | 低 | 一个定时器 |
   | 截图标注编辑器 | 中 | 图像缓冲区 + 撤销栈 |
   | 选中文本工具栏 | 中 | UI Automation + 浮层 |
   | 长截图 / 滚动拼接 | 中高 | 自动滚动 + 帧拼接 |
   | 本地 OCR（PP-OCR 级） | 高 | **必须做成按需下载的可选组件**，否则主程序直接回到 26 MB 级 |
   | Infinite Mouse 跨机鼠标 | 很高 | 网络栈 + 协议 + 配对 + 加密，是 Aitiy 四条重量来源之一 |

   两条决策依据留档：① 社区对 Aitiy 的 Lua 引擎、定时任务、Infinite Mouse、输入统计**未找到任何一条评论提及**——这些功能没换来口碑；② 但「没口碑」不等于「不需要」，代价低的那几项集成进来几乎不侵蚀稳定性，属于低风险收益。
