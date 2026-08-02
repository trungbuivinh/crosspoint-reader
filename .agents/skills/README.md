# CrossPoint Reader — Codex skills

Codex discovers these repository-scoped skill adapters automatically. Each
adapter intentionally delegates to the corresponding `.claude/skills/` source,
so the Claude and Codex workflows cannot drift while both agent surfaces are in
use.

| Skill | Use for |
|---|---|
| `heap-discipline` | ESP32-C3 allocation, buffer, cache, and lifecycle work |
| `control-flow-clarity` | C/C++ state and branching logic |
| `hal-and-abstractions` | storage, input, display, settings, i18n, or rendering |
| `scope-discipline` | proposed features, activities, libraries, settings, or dependencies |
| `refactor-for-review` | narrowly scoped refactors and PR-ready changes |
| `crosspoint-upstream-release` | new stable upstream ports and fork-release preparation |

Use `$<skill-name>` to invoke a workflow explicitly when needed.
