# ADR-0003: 依赖策略——系统自带优先，最少第三方

- Status: accepted
- Date: 2026-10-09

## Context

本工程要在**单文件便携、静态 CRT、无 VC++ 运行库依赖**的前提下实现一批**通用功能**：哈希摘要、HTTP、ZIP 解压、OCR、自动更新、崩溃诊断、日志、单元测试、设置界面与图标。

作者的技术路线点名了几组库（LibTomCrypt + LibTomMath、WinINet、miniz、Nayuki QR、WebView2、Mongoose 可选）。同时本工程有一条来自调研的硬约束：**轻量是产品定义的一部分**——参考程序 MouseInc 靠 1.19 MiB 单文件与空闲 6 MB 工作集拿到了「轻量稳定」的口碑，而 Aitiy 因为内嵌 ONNX Runtime + OCR 模型 + 自研渲染栈导致空闲私有提交 130–176 MB 抖动，被社区集中批评。

在这个约束下，「照单引入规格点名的库」并不总是最优：有些库可以被 **Windows 自带 API** 完全替代（零体积、零许可证义务），有些则确实不可替代。

## Decision

1. **通用功能一律先查轮子，且必须覆盖「Windows 系统自带 API」这一档**；表格对比 2–3 个候选后**先同步 owner 再集成**。规则写入 `AGENTS.md`。
2. 已照准的具体选型（完整对比见本文附录）：
   - **哈希摘要用 Windows BCrypt/CNG**，替代规格点名的 LibTomCrypt + LibTomMath（后者本项目根本不需要）。
   - **OCR 主用 Windows 系统 OCR**（`Windows.Media.Ocr`，主程序零字节增量），高质量需求走**按需下载的可选组件**（PP-OCRv6 tiny，独立进程）。ONNX Runtime 不编进主程序。
   - **保留 miniz**（系统压缩 API 无 DEFLATE，解不了标准 zip）、**保留 WinINet**（本身就是系统自带）。
   - **自动更新与崩溃诊断各自自研**：调研确认单文件 Windows 程序没有可用的现成轮子（详见附录）。
   - **单元测试用 doctest**（CMake/CTest 原生、单头、模板最轻）。
   - **设置界面用原生 HTML/CSS/JS 零构建**，图标用 Phosphor（网页）+ Segoe Fluent Icons（原生）；**不引入 Mongoose**（改用 WebView2 的虚拟主机映射）。
3. 每个新依赖登记 `THIRD-PARTY-NOTICES.md`，并记录**体积与内存增量的实测数字**。
4. **只有确认没有轮子时才自研**，并说明原因。

## Consequences

- 少引入 3 个第三方库（LibTomCrypt、LibTomMath、Mongoose），多个功能直接落到系统 API 上——这是本项目「轻量」承诺的主要来源之一。
- 代价：两个自研模块（更新器、崩溃转储）需要我们自己对正确性负责：更新器要实测自替换流程，崩溃转储要有 **PDB 归档**才有意义（否则 dump 无法解析）。这两条已作为验收断言写入 Issue 跟踪（T-0031 / T-0032）。
- 代价：系统 OCR 在 MS 文档里属「需要 package identity」的用法，实际可用但**不受官方支持**——必须按「探测可用则用、否则降级」实现，不能假设成功。这条已作为硬约束写入 `CONTEXT.md`。
- 代价：`doctest` 与零构建前端意味着**不引入 npm 工具链**，网页部分要手写 DOM 更新；界面复杂度若显著上升，需重新评估（届时按 `AGENTS.md` 的「依赖与技术选型纪律」重新走一遍确认流程）。

## Alternatives considered

**照单引入规格点名的全部库**：最省事、与需求基准字面一致。否决原因：LibTomCrypt + LibTomMath 为几行摘要计算引入两个静态库、Mongoose 为一个本地页面引入一个 HTTP/WebSocket 服务器，而两者都能被零依赖方案替代；在「轻量」是产品定义的前提下，这类引入没有对价。

**为自动更新引入 WinSparkle**：成熟、有签名机制。否决原因：单 DLL 就 2.2–2.8 MB，已超过 MouseInc 整包体量，且必须多带一个文件，直接破坏「单文件 portable」这条硬约束。

**为崩溃上报引入 Crashpad / sentry-native**：功能成熟。否决原因：Crashpad 必须随包一个 handler 子进程且构建不走 CMake；sentry-native 默认指向云端，与「不上传隐私」承诺冲突，且同样需要额外进程。

**引入 webpack/Vite 前端工程**：与 MouseInc.Settings 同路线，组件化体验更好。否决原因：为一个「表单 + 列表」的设置页引入整套 Node 工具链，收益与 C++ 工程的可审查性、体积、离线可控性成反比；已留 Vue 3 作为备选，若界面复杂度上升可再评估。

---

## 附录：选型对比详表（2026-10-09 owner 全部照准）

> 本附录是 2026-10-09 调研结论的原表，随 `docs/execution-plan.md` 于 2026-10-11 解散而归入本 ADR。**表中的选型决策已是定论，不要在没有新证据的情况下重新选型**；若发现某项在实践中不成立，按 `AGENTS.md` 的「依赖与技术选型纪律」重新调研并先同步 owner。

| 领域 | 作者规格点名 | 调研结论 | 关键依据 |
|---|---|---|---|
| **哈希与摘要** | LibTomCrypt + LibTomMath | **改用 Windows BCrypt/CNG**（系统自带） | BCrypt 覆盖 MD5/SHA-1/SHA-256（Vista+），**0 字节增量、0 许可证登记**；LibTomMath 本项目根本不需要（官方 README 称 MPI 仅在构建 binaries 时需要）。签名验证用 CNG 验签或 `WinVerifyTrust` |
| **HTTP** | WinINet | **保留 WinINet 即可**（追求更轻可换 WinHTTP，但代理行为需注意） | 两者都是系统自带，第三方只剩 libcurl——官自述静态构建「not for the faint of heart」且警告勿混用 CRT，排除。若换 WinHTTP 需在 CONTEXT 记为偏离规格 |
| **ZIP 解压** | miniz | **保留 miniz**（不可替代） | Windows 的 Compression API 只有 MSZIP/XPRESS/LZMS **没有 DEFLATE**，解不了 .zip；ntdll 的 raw DEFLATE 仅 Win11 24H2+ 且未公开。**可选组件包格式若改由我方定义（MSZIP/LZMS），则可完全不用 miniz** |
| **OCR** | 「可部署的本地引擎或自有服务」 | **主用 Windows 系统 OCR（`Windows.Media.Ocr`），高质量需求走按需下载的可选组件（PP-OCRv6 tiny，独立进程）** | 系统 OCR **主程序 0 字节增量**（语言数据在系统侧，本机实测 zh-cn 为 2.3 MB），本机实测可识别、1000×140 图 31 ms；ONNX Runtime + PP-OCR 约 24 MB 且是 Aitiy 130–176 MB 空闲内存的来源；Tesseract 中文质量口碑差且 chi_sim 数据 2.47–44 MB |
| **自动更新** | 「自有服务、签名密钥、发布包」 | **无合适轮子 → 自研最小更新器** | 唯一原生候选 WinSparkle 单 DLL 就 2.2–2.8 MB（大于 MouseInc 整包）且破坏单文件；Velopack/Squirrel/Omaha 都以「安装器 + 目录布局」为前提，与单文件 portable **架构互斥**；Squirrel 已停滞、Omaha 已归档 |
| **崩溃上报** | 「更新机制」同阶段 | **无合适轮子 → 用系统 dbghelp `MiniDumpWriteDump` + 本地环形日志 + 手动导出诊断包** | Crashpad 必须随包另一个 handler 子进程且构建不走 CMake；sentry-native 默认指向云端且仍要额外进程；WER LocalDumps 需管理员写 HKLM，便携场景不可用 |
| **日志库** | 要求「Logging：错误和诊断信息」 | **自研最小日志**（不进 spdlog） | 轮转上限与诊断包导出本就要自己写；句柄数/私有提交/线程数快照是纯 Win32；spdlog 只省「写文件+按大小轮转」却带来 fmt 翻译单元与必须 CI 量化的体积 |
| **单元测试** | 规格 §17 要求自动化测试 | **doctest** | CMake/CTest 原生（`add_test`）、单头文件、模板最轻；GoogleTest 需编译成库、Catch2 非单头；**MS 自带 CppUnitTestFramework 需要 VS Native Test 项目 + vstest，Ninja 下不适用**。测试 exe 不进发行物，故与 /MT 约束无关 |
| **设置界面前端栈** | 本地 HTML/CSS/JS | **原生 HTML/CSS/JS（零构建）** | 界面是表单+列表+颜色/热键录入，不需要响应式框架；零 Node/npm、零体积税、产物可直接提交仓库。Vue 3 + Vite 作备选（MouseInc 路线，但需整套 npm 链） |
| **图标** | — | **设置界面用 Phosphor（按需挑 SVG，MIT，本机已装）；原生/托盘用 Segoe Fluent Icons（系统自带）** | Phosphor 60 个图标约 21 KB；Segoe 字体零体积但**不得随包分发**，且 Win10 会回落到 Segoe MDL2 |
| **本地 HTTP 服务** | 「如采用本地 HTTP/WebSocket 则用 Mongoose」 | **不需要 Mongoose** | WebView2 官方提供 `SetVirtualHostNameToFolderMapping`，把本地 `ui/` 目录映射为虚拟域名即可离线加载，比自建回环服务少一个监听端口和一个依赖 |

由本表结论直接产生的任务调整：原「引入 LibTomCrypt + LibTomMath」的任务**取消**，改为使用 BCrypt（阶段 4 文本摘要动作时落地）；原「可选引入 Mongoose」**取消**；wininet 保留（是否换 WinHTTP 是待决策票，见 Issue 跟踪）。
