# Implementation Plan

Living plan for building out `carve`. [CARVE_DESIGN.md](../CARVE_DESIGN.md) is the
architecture (the *what* and *why*); this document tracks *status* and the
*ordered remaining work* (the *when* and *in what order*). It supersedes the
month-by-month sketch in CARVE_DESIGN.md §11. Update it as milestones land; keep
it honest.

Last reviewed: 2026-06-24.

## Status snapshot

Legend: ✅ done & tested · 🟡 partial · ⬜ not started.

| Capability                                                    | State | Notes                                                                                            |
| ------------------------------------------------------------- | ----- | ------------------------------------------------------------------------------------------------ |
| `io` (atomic write, read)                                     | ✅     |                                                                                                  |
| `process` (subprocess capture)                                | ✅     | POSIX; Windows later                                                                             |
| `cdb` (model + JSON + atomic write)                           | ✅     | deterministic output                                                                             |
| `command` (de-Bazel argv)                                     | 🟡     | M2 quirks landed; nvcc/emscripten/cross-host canonicalization left                               |
| `aquery` (proto parse, param-file expand, path resolve)       | ✅     | vendored trimmed `analysis_v2.proto`                                                             |
| `sidecar` (schema, Load/Save, diff, project-scoped merge)     | ✅     | `HeaderIndex` built & persisted; `written_at` stamped                                            |
| `refresh` (in-process aquery, execroot, merge, multi-project) | ✅     | M1 done: scan-deps, incremental, staleness, header index, `--jobs`                               |
| `scan_deps` (clang `DependencyScanningTool`)                  | ✅     | wired into `refresh`; gated linux+macos                                                          |
| `cli` + `//carve:carve`                                       | ✅     | all four subcommands wired: `refresh` + `prune` + `aggregate` + `shard`                          |
| e2e harness, CI, pre-commit, hermetic-llvm, proto matchers    | ✅     |                                                                                                  |
| Layer B (`carve_refresh` rule)                                | ✅     | `bazel run //:refresh`; run-based (nested-bazel resolved)                                        |
| Layer C (aspect + shards)                                     | ✅     | `cc_carve_aspect` + `carve_aspect_refresh` + `shard` + `aggregate`; per-action cacheable shards  |
| Differential harness vs Hedron / clangd validation            | ✅     | `tools/cdb_diff.py` + `docs/differential-report.md` (M3)                                         |
| Distribution (`.bcr/`, prebuilt binaries, release)            | 🟡     | release scaffolding landed (`.bcr/` + release/publish workflows, source-only); not yet published |
| Windows                                                       | ⬜     |                                                                                                  |

Bottom line: **Layer A produces a correct-shaped CDB with header coverage and
incremental refresh.** scan-deps is wired into `refresh`; unchanged actions
reuse cached headers; editing a header re-scans only its owning actions;
unresolved (unbuilt generated) headers are not cached and are retried;
`written_at` and the persisted `HeaderIndex` are in place; scanning is
parallelized (`--jobs`). **M1–M5 are complete**, plus the `prune`, `aggregate`,
and `shard` subcommands — all four CLI subcommands are wired and tested. All three
layers work: `refresh` (A), `carve_refresh` (B, `bazel run //:refresh`), and the
Layer C aspect (`cc_carve_aspect` + `carve_aspect_refresh`) that emits one
cacheable shard per compile action and aggregates them. **Next: M6 (release +
distribution) — the last milestone.**

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
  `bazel aquery`. Shares `refresh::EntriesFromRecords`.
- ✅ `shard` subcommand (`carve/shard`): the per-action invocation the aspect
  schedules — `carve shard --action_key=… --command_file=… --source=… --out=…`
  de-Bazels the command, resolves Xcode placeholders, scans headers (failed scan
  → unstamped, re-scanned later), and writes a one-record shard whose shape
  matches `refresh`. Verified end-to-end: `shard` → `aggregate` → valid CDB.
- ✅ The emitting aspect (`rules/cc_carve_aspect.bzl`): `cc_carve_aspect` walks
  the cc graph and schedules one cacheable `carve shard` build action per compile
  action, reading the fully-expanded command from `action.argv` (no `@param-file`
  indirection — Bazel expands it). Shards land in the `carve_shards` output group.
  `carve_aspect_refresh` (`rules/carve.bzl`) is the `bazel run` driver: it builds
  the shards, then aggregates them against the real execroot.
- ✅ Per-action invalidation via Bazel: a shard's content is a function of its
  compile command, so the shard action's only input is its `command_file`. Bazel
  re-runs only the shards whose command changed; content edits don't re-shard
  (the entry is unchanged). Shards are not header-scanned (`--scan=false`) — the
  database does not use headers and Bazel owns invalidation. Validated by an
  analysis test in CI (the wiring) and a `manual` `build_test` (run on demand — it
  needs an exec-config carve, i.e. a full from-source LLVM build, too costly per CI
  run), plus manual verification at LLVM-graph scale.

Acceptance: aspect emits shards (done); `carve aggregate` merges them (done);
a compile-command change re-shards only the affected action (done — via Bazel's
action cache on `command_file`). **M5 complete.**

Deferred Layer C refinements (follow-ups, not blockers): scoping the aspect to
first-party targets (the design's `exclude_external_sources`, so it does not shard
the whole external graph); recording headers in shards for a shard-built
`HeaderIndex` (the design's `ASPECT_M` source kind); and a **lean shard tool** —
the aspect runs `carve shard` as an exec-config build tool, but `carve` links the
from-source LLVM/clang (via `scan_deps`) even though `shard --scan=false` never
uses it, so the exec build is a full LLVM compile. Splitting a scan-free `shard`
binary (no LLVM link) would make the per-action tool tiny and the exec build cheap.

### M6 — 0.1 release + distribution
`.bcr/` metadata, release automation, prebuilt binaries for common platforms (design §7); decide Windows in-or-out.

- ✅ Release scaffolding (source-only, mirrors the helly25 house pattern): `.bcr/`
  metadata + `presubmit.yml`; `.github/workflows/release.yml` (numeric-semver tag
  -> `bazel-contrib` release_ruleset) + `publish.yaml` (BCR mirror) +
  `release_prep.sh` (validates tag == MODULE.bazel == CHANGELOG version, emits an
  empty root BUILD, comments the dev `include`, excludes dev paths, builds the
  source tarball, prints Keep-a-Changelog notes). Nothing publishes until a tag is
  pushed. **Prebuilt binaries are out of scope (source-only); see design §7.**
- ⬜ Cut the actual release: bump `MODULE.bazel` to the version, move the
  CHANGELOG `[Unreleased]` section to `## [x.y.z]`, push the tag, approve the BCR
  draft PR. Outward-facing; left to a human.
- ⬜ **Consumability gap (blocker for a real release):** a source-only consumer
  building `//carve:carve` builds the from-source `@llvm-project` libraries that
  `scan_deps` links, but carve scopes those to C++17 via a `per_file_copt` in its
  `.bazelrc`, which does NOT propagate to consumers. Move that scoping into the
  BUILD graph (e.g. copts on the `scan_deps` deps) so the binary builds for a
  consumer without carve's `.bazelrc`. (The `.bcr/presubmit.yml` build of
  `//carve:carve` is the test that will surface this.)

Acceptance: a bzlmod consumer can `bazel_dep(name = "helly25_carve")` and get a working CDB; tagged release.

## Cross-cutting / parallelizable
- ✅ `prune` subcommand (`carve/prune`): GC sidecar rows whose `written_at` is older than `--prune_after_days`; unstamped rows kept. `carve prune --sidecar=... --prune_after_days=30`.
- ✅ `aggregate` subcommand (`carve/aggregate`): merges independently-produced
  sidecars into one CDB offline. ✅ `shard` subcommand (`carve/shard`): builds one
  per-action shard. ✅ `cc_carve_aspect` + `carve_aspect_refresh` (Layer C): the
  aspect schedules `shard` per compile action and aggregates the shards.
- Parallel scan-deps (`--jobs`) — fold into M1.
- Property tests: idempotency (have a dogfood check; codify), cross-host determinism (needs M2 canonicalization).
- Keep [docs/test-plan.md](test-plan.md) at zero open debts.
- Latency reality: Layers A/B re-run `bazel aquery` every refresh (design §3.1); sub-second incrementality on huge repos is a Layer C (M5) property — don't over-promise before then.

## Sequencing rationale
M1 first — it converts the working skeleton into the actual product (incrementality + headers). M2 runs alongside (independent). M3 validates correctness before B/C are layered on. M4 → M5 (B before C, per the design's layering). Release (M6) last. Riskiest dependency (LLVM linkage) and toolchain are already done.
