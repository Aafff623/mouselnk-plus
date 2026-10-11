# Issue tracker: GitHub

Issues、specs 与决策票 for this repo live as GitHub issues. Use the `gh` CLI for all operations.

仓库：`github.com/Aafff623/mouselnk-plus`（公开仓库；`gh` 在 clone 内自动推断仓库，无需显式指定）。

## Conventions

- **Create an issue**: `gh issue create --title "..." --body "..."`. Use a heredoc for multi-line bodies.
- **Read an issue**: `gh issue view <number> --comments`, filtering comments by `jq` and also fetching labels.
- **List issues**: `gh issue list --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'` with appropriate `--label` and `--state` filters.
- **Comment on an issue**: `gh issue comment <number> --body "..."`
- **Apply / remove labels**: `gh issue edit <number> --add-label "..."` / `--remove-label "..."`
- **Close**: `gh issue close <number> --comment "..."`

## 本仓库的约定

- **任务 ID（T-xxxx）单调递增、永不复用**。T-0001～T-0035 已分配（含已完成项）；新任务从 **T-0036** 起。Issue 标题一律带 T-ID 前缀（如 `T-0034 — 「轻量」成为可失败的检查`），保持与源码注释、`temp/handoff/` 交接件、提交信息里 T-ID 的可追溯性。
- **阶段用 milestone 分组**（作者规格的五个阶段）：`阶段 1 — 骨架与核心手势链路` … `阶段 5 — 可选增强`。阶段顺序即执行顺序，里程碑内自上而下做。阶段 0（工具链、托盘空壳、配置系统）已完成，不建 Issue，历史在 git log。
- **标签**：
  - `in-progress` —— 当前正在做的那一个（同一时刻只应有一个）。
  - `decision` —— 决策票：需要 owner 拍板的选项、时机与取舍（不是实现任务）。
  - 五个 canonical triage 标签（`needs-triage` / `needs-info` / `ready-for-agent` / `ready-for-human` / `wontfix`）见 `docs/agents/triage-labels.md`，供 `/triage` 处理新流入 Issue 时使用；计划内的实现任务默认不需要打这些标签。
- **任务状态只有两种**：开着的 Issue（未完成/正在做，由 `in-progress` 标签区分）或已关闭（验收通过后 `gh issue close` 并留验收证据评论）。关闭即历史，不另设存档看板。
- **验收**：任务的断言写在上方 Issue 正文；验收通过后把验证证据（测试输出、真机日志路径）作为评论留在 Issue 里再关闭。
- **2026-10-11 迁移说明**：本仓库此前用 `TODO.md` 看板承载任务状态，已于当日按 owner 决定迁移到本 Issue 跟踪并删除该文件；选型与流程依据同步归位到 `docs/adr/` 与 `AGENTS.md`。

## Pull requests as a triage surface

**PRs as a request surface: no.** _(Set to `yes` if this repo treats external PRs as feature requests; `/triage` reads this flag.)_

When set to `yes`, PRs run through the same labels and states as issues, using the `gh pr` equivalents:

- **Read a PR**: `gh pr view <number> --comments` and `gh pr diff <number>` for the diff.
- **List external PRs for triage**: `gh pr list --state open --json number,title,body,labels,author,authorAssociation,comments` then keep only `authorAssociation` of `CONTRIBUTOR`, `FIRST_TIME_CONTRIBUTOR`, or `NONE` (drop `OWNER`/`MEMBER`/`COLLABORATOR`).
- **Comment / label / close**: `gh pr comment`, `gh pr edit --add-label`/`--remove-label`, `gh pr close`.

GitHub shares one number space across issues and PRs, so a bare `#42` may be either — resolve with `gh pr view 42` and fall back to `gh issue view 42`.

## When a skill says "publish to the issue tracker"

Create a GitHub issue.

## When a skill says "fetch the relevant ticket"

Run `gh issue view <number> --comments`.

## Wayfinding operations

Used by `/wayfinder`. The **map** is a single issue with **child** issues as tickets.

- **Map**: a single issue labelled `wayfinder:map`, holding the Notes / Decisions-so-far / Fog body. `gh issue create --label wayfinder:map`.
- **Child ticket**: an issue linked to the map as a GitHub sub-issue (`gh api` on the sub-issues endpoint). Where sub-issues aren't enabled, add the child to a task list in the map body and put `Part of #<map>` at the top of the child body. Labels: `wayfinder:<type>` (`research`/`prototype`/`grilling`/`task`). Once claimed, the ticket is assigned to the driving dev.
- **Blocking**: GitHub's **native issue dependencies** — the canonical, UI-visible representation. Add an edge with `gh api --method POST repos/<owner>/<repo>/issues/<child>/dependencies/blocked_by -F issue_id=<blocker-db-id>`, where `<blocker-db-id>` is the blocker's numeric **database id** (`gh api repos/<owner>/<repo>/issues/<n> --jq .id`, _not_ the `#number` or `node_id`). GitHub reports `issue_dependencies_summary.blocked_by` (open blockers only — the live gate). Where dependencies aren't available, fall back to a `Blocked by: #<n>, #<n>` line at the top of the child body. A ticket is unblocked when every blocker is closed.
- **Frontier query**: list the map's open children (`gh issue list --state open`, scoped to the map's sub-issues / task list), drop any with an open blocker (`issue_dependencies_summary.blocked_by > 0`, or an open issue in the `Blocked by` line) or an assignee; first in map order wins.
- **Claim**: `gh issue edit <n> --add-assignee @me` — the session's first write.
- **Resolve**: `gh issue comment <n> --body "<answer>"`, then `gh issue close <n>`, then append a context pointer (gist + link) to the map's Decisions-so-far.
