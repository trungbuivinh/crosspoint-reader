---
name: crosspoint-upstream-release
description: Safely base Trung Bui's CrossPoint Reader fork on a new stable upstream release while retaining the Google Drive exact-mirror and dual-source OTA features plus their production-hardening fixes. Use this skill whenever the user mentions a new original/upstream CrossPoint release, syncing or upgrading the fork, creating a new release branch/version, porting the two custom features, resolving upstream conflicts, preparing firmware.bin, or publishing a fork release—even if they do not explicitly ask for a skill.
compatibility: Requires git, GitHub CLI, clang-format 21+, CMake/Ninja, PlatformIO, access to origin and upstream remotes, and an X4 device for the final hardware gate.
---

# CrossPoint Upstream Release

Move the personal fork to a new **stable** upstream release without losing either
custom feature or weakening the safeguards added after PRs #4 and #6. Treat this
as a compatibility port onto a new codebase, not as a mechanical replay of old
files.

## Load the project rules first

Before changing branches or files, read:

- `.skills/SKILL.md`, `CLAUDE.md`, and `SCOPE.md`.
- `docs/personal-fork-features.md`, `docs/google-drive-sync.md`, and
  `docs/contributing/testing-debugging.md`.
- `.claude/skills/heap-discipline/SKILL.md`,
  `.claude/skills/hal-and-abstractions/SKILL.md`,
  `.claude/skills/control-flow-clarity/SKILL.md`, and
  `.claude/skills/refactor-for-review/SKILL.md`.
- [references/feature-invariants.md](references/feature-invariants.md) for the
  behavior and safety contract that must survive the port.

Tell the user when one of these rules changes the implementation or stops the
release. Firmware safety decisions must be visible, not implicit.

## Non-negotiable release model

- Use the release returned by upstream GitHub `/releases/latest`. Reject draft,
  pre-release, RC, malformed, or untagged candidates. Never use `develop`.
- Base from the stable tag's commit, not from a moving branch that may contain
  unreleased work.
- Derive the fork version as `<upstream-major>.<minor>.<patch>.0`; the branch,
  `[crosspoint] version`, tag, release title, and embedded release version must
  agree exactly.
- Create a fresh `release/<fork-version>` base. Never rebase or force-push an
  existing historical release branch.
- Push only to `origin` (`trungbuivinh/crosspoint-reader`). The `upstream`
  remote is read-only for this workflow.
- Preserve the new upstream architecture. If upstream replaced its OTA,
  downloader, TLS, settings, storage, rendering, or JSON implementation, port
  the custom behavior into that implementation. Never overwrite it wholesale
  with the old release's files.
- Do not tag, publish, or call the image production-ready until every automated
  gate passes and the X4 checklist is complete.

## Phase 1: establish trusted inputs

1. Run read-only preflight checks:

   ```sh
   git status --short --branch
   git remote -v
   git branch --show-current
   git tag --list --sort=-version:refname
   ```

   Stop on an unexpected dirty tree, detached HEAD, missing remote, or remote
   whose repository does not match the expected fork/upstream pair. Do not
   stash, discard, or repurpose user changes without explicit permission.

2. Fetch both repositories and tags only after repository identity is proven:

   ```sh
   git fetch --prune --tags upstream
   git fetch --prune --tags origin
   ```

3. Resolve the stable upstream release through GitHub's release API, record its
   tag and target commit, and verify the local tag resolves to that commit. The
   upstream tag must be exactly three numeric components (for example `1.5.0`).

4. Identify the previous production branch and its three-component upstream
   base. Record both commit IDs before creating anything. Refuse to proceed if
   the previous fork branch cannot be tied to a known stable upstream tag.

5. Propose the derived fork version and branch to the user before mutating Git.
   The safe default is:

   ```text
   upstream tag:  X.Y.Z
   fork version:  X.Y.Z.0
   release base:  release/X.Y.Z.0 at upstream tag X.Y.Z
   port branch:   feature/port-personal-features-X-Y-Z
   ```

   Create the release base from the upstream tag and the port branch from that
   base. Keep port work reviewable through a PR into the new release branch.

## Phase 2: inventory the old delta and new upstream

Do not assume that every commit after the old upstream tag still belongs in the
fork. Build an explicit inventory:

```sh
git log --reverse --no-merges <old-upstream-tag>..<previous-release-branch>
git diff --name-status <old-upstream-tag>..<previous-release-branch>
git diff --stat <old-upstream-tag>..<new-upstream-tag>
```

Classify each old change as one of:

- Google Drive feature behavior.
- Dual-source OTA behavior.
- A safety fix required by those features (including PR #4/#6 invariants).
- Documentation, tests, CI, or versioning that must be updated.
- Already implemented upstream; do not duplicate it.
- Unrelated historical change; exclude it unless the user explicitly retains
  it.

Then inspect the upstream changes on every surface listed in the invariant
reference. Use `git log -S`, `git log -G`, and focused diffs to find renamed or
replaced APIs. A clean cherry-pick is not proof of compatibility.

## Phase 3: port behavior in reviewable commits

Prefer reimplementation against the new upstream interfaces. Use old commits as
behavioral references and tests, not as authority over current code.

1. Port the Google Drive exact-mirror feature. Compile and run its host tests.
2. Port the dual-source OTA selector and version/source logic. Compile and run
   its host tests.
3. Apply the cross-feature production hardening from PR #6 and retain the PR #4
   settings heap fix where upstream has no equivalent. Add regression tests for
   every changed integration point.
4. Update i18n, fork documentation, release CI, and `[crosspoint] version` in
   distinct reviewable commits.

Do not hide behavioral changes in a conflict-resolution commit. Keep pure
formatting separate if it is large enough to obscure logic.

## Conflict-resolution procedure

For each conflict:

1. Read the complete upstream unit and its callers before editing markers.
2. State the upstream intent and the custom invariant that must remain.
3. Rebuild the smallest compatible behavior using upstream's current API.
4. Search all callers and tests for stale assumptions.
5. Run the narrowest relevant test/build before moving to the next conflict.
6. Record the decision in the PR when it affects storage, TLS, OTA partitions,
   rollback, task lifetime, rendering locks, heap/stack use, or deletion.

Never resolve a feature conflict with broad `--ours`, `--theirs`, or by copying
an old directory over the new tree. If the upstream replacement cannot carry a
safety invariant, stop and ask for a design decision rather than weakening it.

## Automated release gate

Run the bundled invariant audit first:

```sh
.claude/skills/crosspoint-upstream-release/scripts/audit_fork_invariants.sh X.Y.Z.0
```

Fix audit failures semantically; do not edit code merely to satisfy a string
check. If upstream legitimately changed a durable name, update both the audit
and its corresponding invariant after proving the replacement is equivalent.

Then run the repository checks in this order:

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

If cppcheck itself crashes, rerun it verbose and distinguish a tool crash from a
reported defect. Do not claim a pass from the crashed run.

Also verify:

- `firmware.bin` exists and the embedded release string is exactly the intended
  four-component version.
- `verifyRollbackLater` remains a strong symbol when that Arduino hook is still
  used. If upstream changed rollback integration, prove the new equivalent from
  the ELF and boot flow.
- Firmware/DRAM use and large stack frames did not regress unexpectedly.
- A deliberately wrong tag fails the workflow's tag/version guard.
- PR CI is green, including the aggregate required check.

## Hardware gate and publication

Use a backed-up SD card and a disposable Drive mirror folder for destructive
tests. Complete every X4 item in the invariant reference, including interrupted
OTA, rollback, both update sources, exact-mirror deletion ordering, cancellation,
and post-OTA retention of Google Drive.

Only after the port PR is merged and the hardware evidence is recorded may the
exact four-component tag be pushed. Confirm that the fork release is stable,
not draft/pre-release, and exposes one non-empty asset named exactly
`firmware.bin` through `/releases/latest`.

## Handoff report

Report these fields even when work stops early:

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

Use **blocked before release**, not **production-ready**, whenever any required
evidence is missing.

## Self-review before handoff

- [ ] The upstream base is the verified latest stable tag, not `develop` or a
      moving post-release branch.
- [ ] The old release branch remains untouched and un-rewritten.
- [ ] The new branch/version/tag plan uses the exact four-component `.0`
      convention.
- [ ] Both feature contracts and every PR #4/#6 cross-feature invariant were
      reviewed against the new upstream implementation.
- [ ] Conflict resolutions preserve upstream architecture and are explained in
      the PR; no directory was accepted wholesale as ours/theirs.
- [ ] The invariant script, format, tests, cppcheck, both builds, version/ELF
      checks, and PR CI have recorded results.
- [ ] X4 tests use backups/disposable data and include interruption/rollback.
- [ ] No tag, release, or production-ready claim was made before all evidence
      existed.
