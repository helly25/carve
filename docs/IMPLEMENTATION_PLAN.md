# Implementation Plan

Living plan for building out `carve`. [CARVE_DESIGN.md](../CARVE_DESIGN.md) is the
architecture (the *what* and *why*); this document tracks *status* and the
*ordered remaining work* (the *when* and *in what order*). It supersedes the
month-by-month sketch in CARVE_DESIGN.md §11. Update it as milestones land; keep
it honest.

Last reviewed: 2026-06-24.

## Status snapshot

Legend: ✅ done & tested · 🟡 partial · ⬜ not started.

| Capability                                                    | State | Notes                                                                      |
| ------------------------------------------------------------- | ----- | -------------------------------------------------------------------------- |
| `io` (atomic write, read)                                     | ✅     |                                                                            |
| `process` (subprocess capture)                                | ✅     | POSIX; Windows later                                                       |
| `cdb` (model + JSON + atomic write)                           | ✅     | deterministic output                                                       |
| `command` (de-Bazel argv)                                     | 🟡     | M2 quirks landed; nvcc/emscripten/cross-host canonicalization left         |
| `aquery` (proto parse, param-file expand, path resolve)       | ✅     | vendored trimmed `analysis_v2.proto`                                       |
| `sidecar` (schema, Load/Save, diff, project-scoped merge)     | ✅     | `HeaderIndex` built & persisted; `written_at` stamped                      |
| `refresh` (in-process aquery, execroot, merge, multi-project) | ✅     | M1 done: scan-deps, incremental, staleness, header index, `--jobs`         |
| `scan_deps` (clang `DependencyScanningTool`)                  | ✅     | wired into `refresh`; gated linux+macos                                    |
| `cli` + `//carve:carve`                                       | 🟡     | `refresh` + `prune` + `aggregate`; `shard` is an `Unimplemented` stub (M5) |
| e2e harness, CI, pre-commit, hermetic-llvm, proto matchers    | ✅     |                                                                            |
| Layer B (`carve_refresh` rule)                                | ✅     | `bazel run //:refresh`; run-based (nested-bazel resolved)                  |
| Layer C (aspect + shards)                                     | 🟡     | `aggregate` (offline shard merge) landed; emitting aspect + `shard` remain |
| Differential harness vs Hedron / clangd validation            | ✅     | `tools/cdb_diff.py` + `docs/differential-report.md` (M3)                   |
| Distribution (`.bcr/`, prebuilt binaries, release)            | ⬜     |                                                                            |
| Windows                                                       | ⬜     |                                                                            |

Bottom line: **Layer A produces a correct-shaped CDB with header coverage and
incremental refresh.** scan-deps is wired into `refresh`; unchanged actions
reuse cached headers; editing a header re-scans only its owning actions;
unresolved (unbuilt generated) headers are not cached and are retried;
`written_at` and the persisted `HeaderIndex` are in place; scanning is
parallelized (`--jobs`). **M1–M4 are complete**, plus the `prune` and
`aggregate` subcommands. `refresh` (Layer A) and `carve_refresh` (Layer B) are
the working CDB entry points; `aggregate` merges independently-produced sidecars
offline. **Next: M5 (Layer C — the per-action aspect that emits shards, the last
piece `aggregate` is built to consume; `shard` subcommand) and M6 (release +
distribution).**

## Milestones (dependency-ordered)

### M1 — Wire scan-deps into `refresh` (the keystone)
Realizes incremental refresh and header coverage; everything downstream assumes it.

- ✅ For each compile action, call `scan_deps::ScanDependencies(argv, execroot)`; store results in `ActionRecord.headers`. (#9)
- ✅ Reuse on unchanged actions: `MergeRecords` keeps cached headers, and `refresh` re-scans only added/changed actions (`HasMatchingRecord`). (#10)
- ✅ Set `ActionRecord.written_at` via an injected clock (deterministic in tests) — unblocks `prune`. (#11)
- ✅ Build and persist the `HeaderIndex` (header → owning `action_key`s, canonical owner = lex-min) next to the sidecar so an edited header maps to the action(s) to re-scan (design §4.4–§4.5).
- ✅ Header-staleness invalidation: on refresh, an action whose cached header (or source) changed on disk (mtime past `written_at`) is re-scanned even though its command is unchanged, via `FindReusableRecord` + a `rescanned` set passed to `MergeRecords`. One-second granularity.
- ✅ Missing generated headers: a failed scan leaves the record unstamped so the next refresh re-scans it (cache only a complete scan, design §4.2); `RunRefresh` returns `RefreshStats` and the binary surfaces an unresolved-headers count.
- ✅ Parallelize scanning across actions (`--jobs`, default hardware concurrency): an `absl::Mutex`-guarded worker pool (fully thread-safety-annotated, enforced by `-Wthread-safety`). The scan decision stays serial so the sidecar is deterministic. (Runtime tsan CI is blocked by the hermetic `llvm` toolchain's sanitizer-runtime build; the `.bazelrc` `:tsan` flag is documented + commented out for when it's fixed.)
- ✅ **Platform/optionality decision — resolved (option b).** `scan_deps` is gated linux+macos and injected into `refresh` as a `HeaderScanner`, so the core CDB still builds everywhere and header-scanning is the enhancement; the gate propagates to the `carve` binary and e2e test.

Acceptance: `carve refresh` on this repo populates `headers`; editing a header invalidates only owning actions; refresh stays idempotent; unit + e2e tests cover header population, reuse, and missing-header caching.

### M2 — Complete the de-Bazel quirk inventory (CARVE_DESIGN §4.3)
Independent of M1; can run in parallel. One golden test per quirk (design §9.2).

Done (golden-tested in `carve/command`, wired through `refresh`):
- ✅ `-fno-canonical-system-headers` strip (clangd#1004) and `-gcc-toolchain` strip (clangd#1248).
- ✅ ccache wrapper: drop a leading `ccache` so argv[0] is the real compiler.
- ✅ MSVC `/showIncludes`[`:user`] strip; `-fmodules-cache-path=bazel-out/...` strip.
- ✅ Apple `wrapped_clang` + `__BAZEL_XCODE_*`: `command::ResolveXcodePlaceholders` substitutes the developer-dir/SDK paths; `refresh` invokes an injected `XcodeResolver` (the binary resolves via `xcode-select`/`xcrun` on macOS) only when a command carries a placeholder.
- ✅ `parse_headers` actions are filtered implicitly: their "source" is a header, so `FindSource` finds no TU and the action is skipped.

Remaining (deferred — niche toolchains or a design-level property; best validated against M3's corpus):
- ⬜ Execroot / absolute-path canonicalization to *workspace-relative* (the cross-host determinism property, §9). carve currently emits paths absolute against the execroot, which clangd consumes correctly; workspace-relative rewriting is a separate, larger change.
- ⬜ NVCC→clang flag translation (CUDA); Emscripten driver indirection; Windows command-line-length param-file specifics (carve already expands `@param` files via aquery `--include_param_files`).

Acceptance: golden test per quirk; platform-specific ones skipped where the toolchain is absent.

### M3 — Differential harness + clangd validation ✅
Correctness gate before building Layer B/C on top.

- ✅ `tools/cdb_diff.py`: a normalizing CDB differ (keys by workspace-relative source, ignores volatile output/dep/seed tokens), with a `--selftest` in CI. Diffs carve's CDB against any reference (e.g. a Hedron CDB).
- ✅ clangd verified working on carve itself: `clangd --check` builds the preamble/AST/index with zero compilation diagnostics using a toolchain-matched clang 22 (`docs/differential-report.md`).
- ✅ carve-vs-Hedron differences explained in the report (scan-deps vs `clang -M`, sidecar incrementality, header index, execroot-absolute vs workspace-relative paths). A live Hedron diff just needs a Hedron CDB pointed at `cdb_diff.py`; Hedron is intentionally not wired into carve (the tool it replaces) — a corpus run over external repos remains as optional follow-up.

### M4 — Layer B: `carve_refresh` rule ✅
`bazel run //:refresh` as the CDB entry point (design §4.6).

- ✅ **Nested-bazel concern resolved: a `bazel run` target, not a build action.** carve invokes `bazel aquery`/`bazel info`; spawning bazel inside a build action is the nested-bazel trap (lock/server contention, sandboxing). `carve_refresh` (`rules/carve.bzl`) generates a runfiles-aware launcher that runs the carve binary *after* the outer build and writes the CDB to `$BUILD_WORKSPACE_DIRECTORY`.
- ✅ Dogfooded: `bazel run //:refresh` writes the repo's `compile_commands.json` (22 entries). Analysis-tested (`rules/carve_test.bzl`): the target is runnable and carries the carve binary in its runfiles (no nested bazel in the test).

(The design's literal `bazel build //:compile_commands` shape is not viable for the reason above; the README/usage now shows `bazel run //:refresh`.)

### M5 — Layer C: aspect + shards
Per-action, individually-cacheable shards for huge repos (design §4.7).

- ✅ `aggregate` subcommand (`carve/aggregate`): the offline merge half — unions
  independently-produced sidecars (de-dup by (project_id, action_key), keep
  most-recently-written, deterministic order) and emits one CDB without running
  `bazel aquery`. Shares `refresh::EntriesFromRecords`. Landed ahead of the
  aspect so the consumer is ready and unit-tested.
- ⬜ The emitting aspect: a Starlark aspect over the build graph that, per
  compile action, writes a one-record shard sidecar (the hard part — action
  introspection, scan-deps in/after the action, individual cacheability).
- ⬜ `shard` subcommand: the per-action invocation the aspect calls to produce a
  single shard.

Acceptance: aspect emits shards; `carve aggregate` merges them (done); editing
one source rebuilds only its shard.

### M6 — 0.1 release + distribution
`.bcr/` metadata, release automation, prebuilt binaries for common platforms (design §7); decide Windows in-or-out.

Acceptance: a bzlmod consumer can `bazel_dep(name = "carve")` and get a working CDB; tagged release.

## Cross-cutting / parallelizable
- ✅ `prune` subcommand (`carve/prune`): GC sidecar rows whose `written_at` is older than `--prune_after_days`; unstamped rows kept. `carve prune --sidecar=... --prune_after_days=30`.
- ✅ `aggregate` subcommand (`carve/aggregate`): merges independently-produced
  sidecars into one CDB offline. `shard` (the per-action emitter) lands with M5.
- Parallel scan-deps (`--jobs`) — fold into M1.
- Property tests: idempotency (have a dogfood check; codify), cross-host determinism (needs M2 canonicalization).
- Keep [docs/test-plan.md](test-plan.md) at zero open debts.
- Latency reality: Layers A/B re-run `bazel aquery` every refresh (design §3.1); sub-second incrementality on huge repos is a Layer C (M5) property — don't over-promise before then.

## Sequencing rationale
M1 first — it converts the working skeleton into the actual product (incrementality + headers). M2 runs alongside (independent). M3 validates correctness before B/C are layered on. M4 → M5 (B before C, per the design's layering). Release (M6) last. Riskiest dependency (LLVM linkage) and toolchain are already done.
