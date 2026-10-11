# Claude Code Project Entry

Read and follow the project rules in `AGENTS.md`. It is the shared source for this project; do not create a second competing rule system here.

Pay particular attention to the "复现的法律与工程边界" section of `AGENTS.md`: this project independently reimplements MouseInc/Aitiy behavior and must never contain copied or decompiled code.

Secrets follow the Secrets section of `AGENTS.md`: save on receipt into the Git-ignored temp model, no confirmation, never ask twice.

## Agent skills

### Issue tracker

Issues、specs 与决策票 live as GitHub issues in this repo, operated with the `gh` CLI; task IDs (T-xxxx) are monotonic and phases are milestones. See `docs/agents/issue-tracker.md`.

### Triage labels

默认五个 canonical 角色（`needs-triage` / `needs-info` / `ready-for-agent` / `ready-for-human` / `wontfix`），另有计划管理用的 `in-progress` 与 `decision`。See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: root `CONTEXT.md` plus `docs/adr/`; read both before exploring. See `docs/agents/domain.md`.
