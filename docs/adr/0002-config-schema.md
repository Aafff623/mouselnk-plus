# ADR-0002: 内部配置 schema 采用作者规格的新格式

- Status: accepted
- Date: 2026-10-08

## Context

两件事同时成立，但它们指向不同的配置格式：

1. 作者规格（`temp/input/mouseinc-author-spec.md` 第十四节）明确给出了配置结构示例，并要求 SchemaVersion、支持版本迁移、保留用户自定义内容。其形态是：`Gestures[].{Id, Points:[[x,y],…]}`、`MatchGlobal[].{GestureId, Name, Enabled, Actions:[{Type, …}]}`、`MatchCustom[].{Programs, IgnoreGlobal, Bindings}`、动作是**对象**。
2. owner 最初的诉求里包含「能读入既有 MouseInc 配置做迁移」。本机取证的 `MouseInc.json` 实际形态是：扁平 PascalCase、`Gestures[].{Sign, Data:[x1,y1,x2,y2,…]}`（扁平数组）、`MatchGlobal[].{Sign, Name, Valid, Actions:[["SendKeys","Ctrl+C"]]}`（动作是**位置数组**）、应用规则叫 `MatchCustom[].{Name, Match, List, IgnoreGlobal}`、边缘叫 `WheelEdge`、触发角叫 `HotCorner`、另有 `ClipboardManager.Menu`、`Locales` 等。

两者在**字段名、嵌套层次、动作表示**三层都不同。无法既「完全兼容 MouseInc.json」又「采用作者的 schema」，必须选一个作为运行时格式。这是难以逆转的选择（配置是持久化数据，格式定错会让后续每次改动都背迁移包袱）。

## Decision

1. **内部运行时 schema 完全采用作者规格给定的格式**：`SchemaVersion` + 规格列出的顶层键 + 动作的对象表示 `{"Type": …}`。
2. 手势模板内部坐标用**点对数组** `[[x, y], …]`（作者规格的 `Points` 形态），不使用 MouseInc 的扁平数组或 Aitiy 的字符串。
3. `MouseInc.json` **不作为运行时格式**。它只能作为**一次性导入源**（读取 → 转换成我们的 schema），该导入器是可选功能，未排期。
4. `SchemaVersion` 与迁移机制从第一次落盘起就要做（规格明确要求「支持配置版本迁移，保留用户自定义内容」）。

## Consequences

变得更可行：

- 与需求基准一致，不需要在规格之外维护第二套格式语义。
- **动作对象化带来实质收益**：`{"Type":"Execute","Path":"…","Args":"…","Admin":true,"Wait":true}` 天然能表达可选字段，而这些用位置数组很难表达（MouseInc 靠 `["Execute","cmd","admin"]` 这种位置参数约定）。规格第八节要求「校验动作类型、参数类型、数量和范围」与「未知动作明确报错」，对象形态下的校验实现起来直接得多。
- 模板坐标用点对数组后，识别算法的 101 点重采样结果与模板表示同构，测试夹具（fixture）写起来更直白。

代价与约束：

- 用户的既有 `MouseInc.json` 不能直接沿用，迁移从「白拿」变成「需要额外写导入器」。
- 配置从第一版起就必须带 `SchemaVersion`，后续任何字段改动都要走迁移路径，不能随手改字段名。
- 必须明确两套语义的差异点，避免把 MouseInc 的习惯带进来：例如 MouseInc 的 `Valid: false` 表示该绑定**禁用**，我们的等价物是 `Enabled: false`；MouseInc 的 `MouseGesture.Sensitive` 与规格的 `MouseGesture.Sensitivity` 键名不同，范围语义也可能不同（规格：0～100，默认 50）。

## Alternatives considered

**直接采用 MouseInc 的 schema 作为内部格式**
好处是白拿迁移能力与生态兼容，甚至能让用户继续用第三方脚本直接改配置。否决原因：违背需求基准（作者规格是需求基准，不是参考）；位置数组式的动作无法表达可选字段，参数校验只能靠位置约定与手写规则，与规格的校验要求相冲突；MouseInc 的 schema 本身带历史包袱（`Locales` 混在配置里、空 Name 条目用作分隔线、`Valid` 这样的隐晦命名）。

**双格式运行（同时能读写两种）**
表面最讨好，实际复杂度翻倍：两套语义（`IgnoreGlobal` 的默认行为、显式禁用的含义、动作失败后的默认处理）会持续漂移；规格要求「重载使用一致的配置快照」，双格式会让快照、迁移、保存路径三者互相纠缠，且每次新增功能都要在两套格式里各写一遍。

**作者 schema + 一次性导入器（选中）**
守住了需求基准，同时不放弃用户迁移路径。导入器是一次性、独立、单向的代码，不污染运行时 schema，也不需要在运行时承担双格式的语义一致性负担。
