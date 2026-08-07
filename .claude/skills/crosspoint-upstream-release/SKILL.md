---
name: crosspoint-upstream-release
description: Safely base Trung Bui's CrossPoint Reader fork on a new stable upstream release while retaining Google Drive exact-mirror and dual-source OTA behavior plus production hardening. Use for upstream releases, fork upgrades, release branches or versions, feature ports, conflicts, firmware.bin, tags, or GitHub publication.
compatibility: Requires git, GitHub CLI, clang-format 21+, CMake/Ninja, PlatformIO, origin/upstream access, and an X4 device for the final production gate.
---

# CrossPoint Upstream Release

Port the personal fork to a new stable upstream release without losing either
custom feature or weakening its safety fixes. Treat this as a compatibility port
onto a changed codebase, not a replay of old files.

## Load project rules first

Before changing branches or firmware, read:

- `.skills/SKILL.md`, `CLAUDE.md`, `SCOPE.md`, and `AGENTS.md` when present.
- `docs/personal-fork-features.md`, `docs/google-drive-sync.md`, and
  `docs/contributing/testing-debugging.md`.
- The heap, HAL, control-flow, and review skills under `.claude/skills/`.
- [references/feature-invariants.md](references/feature-invariants.md).

Tell the user whenever these rules change implementation or stop publication.

## Non-negotiable release model

- Resolve upstream GitHub `/releases/latest`; reject drafts, pre-releases, RCs,
  malformed releases, moving branches, and unverified tags.
- Base the release from the stable tag's peeled commit, never `develop`.
- Use a fresh `release/<fork-version>` base and a review branch. Never rewrite a
  historical release branch.
- Fork versions are four numeric components. The default first fork revision is
  `<upstream-version>.0`; an explicit user-selected revision such as `.1` is
  authoritative when branch, config, tag, title, and embedded version agree.
- Push only to `origin` (`trungbuivinh/crosspoint-reader`). Treat `upstream` as
  read-only.
- Preserve the new upstream architecture. Port custom behavior into its current
  downloader, TLS, OTA, settings, storage, rendering, and parser layers.
- Do not tag, publish, or call an image production-ready until all automated
  gates and the X4 production matrix pass, unless the user explicitly accepts
  and documents a narrower risk exception.

## Phase 1: establish trusted inputs

1. Run read-only preflight checks:

   ```sh
   git status --short --branch
   git remote -v
   git branch --show-current
   git tag --list --sort=-version:refname
   ```

   Stop on an unexpected dirty tree, detached HEAD, missing remote, or wrong
   repository identity. Never discard or hide user work.

2. Fetch both remotes and tags only after identity is proven.
3. Query the upstream release API. Record release URL, tag, peeled commit, draft
   and pre-release flags, and the published firmware asset.
4. Accept upstream tags with an optional leading `v`, but require exactly three
   numeric version components after stripping it. Verify the local tag peels to
   the API target commit; do not trust a same-named stale local tag.
5. Identify and record the previous fork production branch and stable upstream
   base before creating branches.
6. State the proposed upstream tag, fork version, release branch, and port branch
   before mutation. Create both branches from the verified tag commit.

## Phase 2: inventory and map the old delta

Build an explicit inventory:

```sh
git log --reverse --no-merges <old-base>..<previous-release>
git diff --name-status <old-base>..<previous-release>
git diff --stat <old-base>..<new-base>
```

Classify old changes as Drive behavior, OTA behavior, required hardening,
documentation/tests/CI/versioning, already upstream, or unrelated. Exclude
unrelated history unless explicitly retained.

Inspect every new upstream surface named by the invariant reference. Use
focused diffs and `git log -S`/`git log -G` to find replacements. A clean
cherry-pick proves only textual compatibility.

## Phase 3: port in reviewable commits

1. Port Google Drive exact mirror onto current storage, activity, theme, and
   network APIs; run its parser and sync-plan tests.
2. Port the OTA source selector, endpoint mapping, version parser, and release
   URL validation; run OTA tests.
3. Reapply cross-feature hardening: complete streaming JSON, bounded heap/stack,
   render locking, settings-web allocation control, verified TLS, exact size,
   inactive-slot writes, and delayed rollback confirmation.
4. Port icon/i18n/docs, release CI, audit resources, and versioning separately.

For every conflict, read the whole current unit and its callers, state upstream
intent and the retained invariant, implement the smallest compatible behavior,
search for stale assumptions, and run a narrow test. Never resolve feature
conflicts with broad `ours`, `theirs`, or an old-directory copy.

## Automated release gate

Run the invariant audit first:

```sh
.claude/skills/crosspoint-upstream-release/scripts/audit_fork_invariants.sh X.Y.Z.N
```

Then run, in order:

```sh
./bin/clang-format-fix
git diff --check
cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/test
ctest --test-dir build/test --output-on-failure -j
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run -e default
pio run -e gh_release
```

If cppcheck crashes, distinguish a tool crash from reported defects; a crashed
run is not a pass. Also verify:

- `firmware.bin` exists, is non-empty, fits an OTA slot, and embeds the exact
  four-component release version.
- the ELF contains a strong `verifyRollbackLater` symbol while that Arduino hook
  remains active, plus the late confirmation checkpoint.
- bootloader/sdkconfig rollback support and two non-overlapping OTA slots remain.
- TLS peer chain and hostname checks are compiled into the wolfSSL adapter, and
  its temporary SDK patch restores the submodule cleanly.
- firmware/DRAM and stack-usage reports show no unexplained regression.
- a deliberately wrong tag fails the workflow's version guard.
- PR CI, including its aggregate required check, is green.

## Hardware gate and publication

Use a backed-up SD card and disposable Drive mirror. Complete every X4 item in
the invariant reference, especially destructive ordering, cancellation,
interrupted OTA, rollback, both source endpoints, and Drive retention after a
custom OTA.

Only after the port PR is merged and evidence is recorded may the exact tag be
pushed. Verify the resulting GitHub release is stable, not draft/pre-release,
and `/releases/latest` exposes one non-empty asset named `firmware.bin`.

## Handoff report

Always report:

```text
Upstream stable tag and commit:
Previous fork release:
New fork version and branches:
Port commits:
Conflicts and decisions:
Google Drive invariants:
OTA/rollback invariants:
Automated checks:
X4 checks:
Release/API status:
Blockers or residual risks:
```

Use **blocked before release**, not **production-ready**, whenever required
evidence is missing.

## Self-review

- [ ] Verified stable tag commit, not a moving branch.
- [ ] Historical release branch remains untouched.
- [ ] Branch, config, embedded version, tag, and title agree exactly.
- [ ] Both custom contracts and all hardening invariants were semantically mapped.
- [ ] No current upstream subsystem was replaced wholesale by old code.
- [ ] Audit, format, tests, cppcheck, both builds, artifact/ELF checks, and PR CI pass.
- [ ] X4 evidence includes interruption, rollback, deletion ordering, and retention.
- [ ] No tag or stable release was published before all required evidence existed.
