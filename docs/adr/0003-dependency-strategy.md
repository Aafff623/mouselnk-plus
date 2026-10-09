# ADR-0003: 依赖策略——系统自带优先，最少第三方

- Status: accepted
- Date: 2026-10-09

## Context

本工程要在**单文件便携、静态 CRT、无 VC++ 运行库依赖**的前提下实现一批**通用功能**：哈希摘要、HTTP、ZIP 解压、OCR、自动更新、崩溃诊断、日志、单元测试、设置界面与图标。

作者的技术路线点名了几组库（LibTomCrypt + LibTomMath、WinINet、miniz、Nayuki QR、WebView2、Mongoose 可选）。同时本工程有一条来自调研的硬约束：**轻量是产品定义的一部分**——参考程序 MouseInc 靠 1.19 MiB 单文件与空闲 6 MB 工作集拿到了「轻量稳定」的口碑，而 Aitiy 因为内嵌 ONNX Runtime + OCR 模型 + 自研渲染栈导致空闲私有提交 130–176 MB 抖动，被社区集中批评。

在这个约束下，「照单引入规格点名的库」并不总是最优：有些库可以被 **Windows 自带 API** 完全替代（零体积、零许可证义务），有些则确实不可替代。

## Decision

1. **通用功能一律先查轮子，且必须覆盖「Windows 系统自带 API」这一档**；表格对比 2–3 个候选后**先同步 owner 再集成**。规则写入 `AGENTS.md`。
2. 已照准的具体选型（完整对比见 `docs/execution-plan.md` 第 2.2 节）：
   - **哈希摘要用 Windows BCrypt/CNG**，替代规格点名的 LibTomCrypt + LibTomMath（后者本项目根本不需要）。
   - **OCR 主用 Windows 系统 OCR**（`Windows.Media.Ocr`，主程序零字节增量），高质量需求走**按需下载的可选组件**（PP-OCRv6 tiny，独立进程）。ONNX Runtime 不编进主程序。
   - **保留 miniz**（系统压缩 API 无 DEFLATE，解不了标准 zip）、**保留 WinINet**（本身就是系统自带）。
   - **自动更新与崩溃诊断各自自研**：调研确认单文件 Windows 程序没有可用的现成轮子（详见 §2.2）。
   - **单元测试用 doctest**（CMake/CTest 原生、单头、模板最轻）。
   - **设置界面用原生 HTML/CSS/JS 零构建**，图标用 Phosphor（网页）+ Segoe Fluent Icons（原生）；**不引入 Mongoose**（改用 WebView2 的虚拟主机映射）。
3. 每个新依赖登记 `THIRD-PARTY-NOTICES.md`，并记录**体积与内存增量的实测数字**。
4. **只有确认没有轮子时才自研**，并说明原因。

## Consequences

- 少引入 3 个第三方库（LibTomCrypt、LibTomMath、Mongoose），多个功能直接落到系统 API 上——这是本项目「轻量」承诺的主要来源之一。
- 代价：两个自研模块（更新器、崩溃转储）需要我们自己对正确性负责：更新器要实测自替换流程，崩溃转储要有 **PDB 归档**才有意义（否则 dump 无法解析）。这两条已写进执行计划的阶段 5 与风险登记。
- 代价：系统 OCR 在 MS 文档里属「需要 package identity」的用法，实际可用但**不受官方支持**——必须按「探测可用则用、否则降级」实现，不能假设成功。这条已作为硬约束写入 `CONTEXT.md`。
- 代价：`doctest` 与零构建前端意味着**不引入 npm 工具链**，网页部分要手写 DOM 更新；界面复杂度若显著上升，需重新评估（届时走 §3.1 流程）。

## Alternatives considered

**照单引入规格点名的全部库**：最省事、与需求基准字面一致。否决原因：LibTomCrypt + LibTomMath 为几行摘要计算引入两个静态库、Mongoose 为一个本地页面引入一个 HTTP/WebSocket 服务器，而两者都能被零依赖方案替代；在「轻量」是产品定义的前提下，这类引入没有对价。

**为自动更新引入 WinSparkle**：成熟、有签名机制。否决原因：单 DLL 就 2.2–2.8 MB，已超过 MouseInc 整包体量，且必须多带一个文件，直接破坏「单文件 portable」这条硬约束。

**为崩溃上报引入 Crashpad / sentry-native**：功能成熟。否决原因：Crashpad 必须随包一个 handler 子进程且构建不走 CMake；sentry-native 默认指向云端，与「不上传隐私」承诺冲突，且同样需要额外进程。

**引入 webpack/Vite 前端工程**：与 MouseInc.Settings 同路线，组件化体验更好。否决原因：为一个「表单 + 列表」的设置页引入整套 Node 工具链，收益与 C++ 工程的可审查性、体积、离线可控性成反比；已留 Vue 3 作为备选，若界面复杂度上升可再评估。
