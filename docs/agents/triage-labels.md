# Triage Labels

The skills speak in terms of five canonical triage roles. This file maps those roles to the actual label strings used in this repo's issue tracker.

| Label in mattpocock/skills | Label in our tracker | Meaning                                  |
| -------------------------- | -------------------- | ---------------------------------------- |
| `needs-triage`             | `needs-triage`       | Maintainer needs to evaluate this issue  |
| `needs-info`               | `needs-info`         | Waiting on reporter for more information |
| `ready-for-agent`          | `ready-for-agent`    | Fully specified, ready for an AFK agent  |
| `ready-for-human`          | `ready-for-human`    | Requires human implementation            |
| `wontfix`                  | `wontfix`            | Will not be actioned                     |

When a skill mentions a role (e.g. "apply the AFK-ready triage label"), use the corresponding label string from this table.

本仓库 2026-10-11 起使用默认词表，未做改名。

除这五个 canonical 角色外，本仓库另有**计划管理用**的标签（不属于 triage 词表，见 `docs/agents/issue-tracker.md`）：

| 标签 | 用途 |
|---|---|
| `in-progress` | 当前正在实现的任务（同一时刻只应有一个） |
| `decision` | 决策票：需要 owner 拍板的选项与时机 |

阶段分组用 **milestone**（作者规格的五个阶段），不用标签。
