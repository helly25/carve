# Implementation Plan

Living plan for building out `carve`. [CARVE_DESIGN.md](../CARVE_DESIGN.md) is the
architecture (the *what* and *why*); this document tracks *status* and the
*ordered remaining work* (the *when* and *in what order*). It supersedes the
month-by-month sketch in CARVE_DESIGN.md §11. Update it as milestones land; keep
it honest.

Last reviewed: 2026-06-23.

## Status snapshot

Legend: ✅ done & tested · 🟡 partial · ⬜ not started.

| Capability | State | Notes |
| --- | --- | --- |
| `io` (atomic write, read) | ✅ | |
| `process` (subprocess capture) | ✅ | POSIX; Windows later |
| `cdb` (model + JSON + atomic write) | ✅ | deterministic output |
| `command` (de-Bazel argv) | 🟡 | 2 of ~12 quirks (`-fno-canonical-system-headers`, `-gcc-toolchain`) |
| `aquery` (proto parse, param-file expand, path resolve) | ✅ | vendored trimmed `analysis_v2.proto` |
| `sidecar` (schema, Load/Save, diff, project-scoped merge) | ✅ | `HeaderIndex` defined but unused; `written_at` unset |
| `refresh` (in-process aquery, execroot, merge, multi-project) | 🟡 | works; **does not call scan-deps** → headers never populated |
| `scan_deps` (clang `DependencyScanningTool`) | 🟡 | works standalone; **not wired into `refresh`**; gated linux+macos |
| `cli` + `//carve:carve` | 🟡 | `refresh` only; `aggregate`/`shard`/`prune` are `Unimplemented` stubs |
| e2e harness, CI, pre-commit, hermetic-llvm, proto matchers | ✅ | |
| Layer B (`cc_carve` rule) | ⬜ | |
| Layer C (aspect + shards) | ⬜ | |
| Differential harness vs Hedron / clangd validation | ⬜ | design wanted this early |
| Distribution (`.bcr/`, prebuilt binaries, release) | ⬜ | |
| Windows | ⬜ | |

Bottom line: **Layer A produces a correct-shaped CDB end-to-end, but without
header coverage or real incrementality** — because scan-deps is built and proven
yet not yet called by `refresh`. That gap is M1 and unlocks the core value.

## Milestones (dependency-ordered)

### M1 — Wire scan-deps into `refresh` (the keystone)
Realizes incremental refresh and header coverage; everything downstream assumes it.

- For each compile action, call `scan_deps::ScanDependencies(argv, execroot)`; store results in `ActionRecord.headers`.
- Reuse on unchanged actions is already handled by `MergeRecords` (same key+command keeps cached headers) — verify only changed/added actions are re-scanned.
- Set `ActionRecord.written_at` (inject the clock for deterministic tests) — also unblocks `prune`.
- Build and persist the bi-directional `HeaderIndex` (header → owning `action_key`s, canonical owner = lex-min) so an edited header maps to the action(s) to re-scan (design §4.4–§4.5).
- Missing generated headers: cache only when no headers are missing (design §4.2); surface a count.
- Parallelize scanning across actions (`--jobs`, default hardware concurrency).
- **Decision required — platform/optionality:** `refresh` is cross-platform but `scan_deps` is gated linux+macos. Options: (a) gate the whole `carve` binary to linux+macos (matches the support matrix, simplest); (b) make scan-deps an opt-in build/runtime mode so the core CDB still builds everywhere and header-scanning is an enhancement (preserves the §4.2 "works without scan-deps" decoupling). Recommend (b).

Acceptance: `carve refresh` on this repo populates `headers`; editing a header invalidates only owning actions; refresh stays idempotent; unit + e2e tests cover header population, reuse, and missing-header caching.

### M2 — Complete the de-Bazel quirk inventory (CARVE_DESIGN §4.3)
Independent of M1; can run in parallel. One golden test per quirk (design §9.2).

- Execroot / absolute-path canonicalization (also the precondition for cross-host determinism and clean clangd paths).
- Apple `wrapped_clang` + `__BAZEL_XCODE_*`; NVCC→clang; MSVC `/showIncludes`; ccache symlink; Windows command-line length; `//external` symlink/execroot; `parse_headers`/`layering_check`/`compiler_param_file` action filtering.

Acceptance: golden test per quirk; platform-specific ones skipped where the toolchain is absent.

### M3 — Differential harness + clangd validation
Correctness gate before building Layer B/C on top.

- Run `carve` vs Hedron over a corpus (this repo + a couple of OSS Bazel repos); diff CDBs; document every meaningful difference (design §8, §9.4).
- Open the produced CDB in clangd against a real target; confirm navigation/diagnostics/background indexing work.

Acceptance: a committed diff report with differences explained; clangd verified working on carve itself.

### M4 — Layer B: `cc_carve` rule
`bazel build //:compile_commands` as the CDB entry point (design §4.6).

- Resolve the nested-bazel concern up front (running aquery inside a build action vs a `bazel run`/genrule shape).

Acceptance: `bazel build //:compile_commands` emits the CDB; integration test over a synthetic workspace.

### M5 — Layer C: aspect + shards
Per-action, individually-cacheable shards for huge repos (design §4.7); implements `aggregate`/`shard`.

Acceptance: aspect emits shards; `carve aggregate` merges; editing one source rebuilds only its shard.

### M6 — 0.1 release + distribution
`.bcr/` metadata, release automation, prebuilt binaries for common platforms (design §7); decide Windows in-or-out.

Acceptance: a bzlmod consumer can `bazel_dep(name = "carve")` and get a working CDB; tagged release.

## Cross-cutting / parallelizable
- `prune` subcommand (GC by `written_at`; needs M1's timestamps).
- `aggregate` / `shard` subcommands (land with M5).
- Parallel scan-deps (`--jobs`) — fold into M1.
- Property tests: idempotency (have a dogfood check; codify), cross-host determinism (needs M2 canonicalization).
- Keep [docs/test-plan.md](test-plan.md) at zero open debts.
- Latency reality: Layers A/B re-run `bazel aquery` every refresh (design §3.1); sub-second incrementality on huge repos is a Layer C (M5) property — don't over-promise before then.

## Sequencing rationale
M1 first — it converts the working skeleton into the actual product (incrementality + headers). M2 runs alongside (independent). M3 validates correctness before B/C are layered on. M4 → M5 (B before C, per the design's layering). Release (M6) last. Riskiest dependency (LLVM linkage) and toolchain are already done.
