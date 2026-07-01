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
and `shard` subcommands - all four CLI subcommands are wired and tested. All three
layers work: `refresh` (A), `carve_refresh` (B, `bazel run //:refresh`), and the
Layer C aspect (`cc_carve_aspect` + `carve_aspect_refresh`) that emits one
cacheable shard per compile action and aggregates them. **Next: M6 (release +
distribution) - the last milestone.**

## Milestones (dependency-ordered)

### M1 - Wire scan-deps into `refresh` (the keystone)
Realizes incremental refresh and header coverage; everything downstream assumes it.

- ✅ For each compile action, call `scan_deps::ScanDependencies(argv, execroot)`; store results in `ActionRecord.headers`. (#9)
- ✅ Reuse on unchanged actions: `MergeRecords` keeps cached headers, and `refresh` re-scans only added/changed actions (`HasMatchingRecord`). (#10)
- ✅ Set `ActionRecord.written_at` via an injected clock (deterministic in tests) - unblocks `prune`. (#11)
- ✅ Build and persist the `HeaderIndex` (header → owning `action_key`s, canonical owner = lex-min) next to the sidecar so an edited header maps to the action(s) to re-scan (design §4.4–§4.5).
- ✅ Header-staleness invalidation: on refresh, an action whose cached header (or source) changed on disk (mtime past `written_at`) is re-scanned even though its command is unchanged, via `FindReusableRecord` + a `rescanned` set passed to `MergeRecords`. One-second granularity.
- ✅ Missing generated headers: a failed scan leaves the record unstamped so the next refresh re-scans it (cache only a complete scan, design §4.2); `RunRefresh` returns `RefreshStats` and the binary surfaces an unresolved-headers count.
- ✅ Parallelize scanning across actions (`--jobs`, default hardware concurrency): an `absl::Mutex`-guarded worker pool (fully thread-safety-annotated, enforced by `-Wthread-safety`). The scan decision stays serial so the sidecar is deterministic. (Runtime tsan CI is blocked by the hermetic `llvm` toolchain's sanitizer-runtime build; the `.bazelrc` `:tsan` flag is documented + commented out for when it's fixed.)
- ✅ **Platform/optionality decision - resolved (option b).** `scan_deps` is gated linux+macos and injected into `refresh` as a `HeaderScanner`, so the core CDB still builds everywhere and header-scanning is the enhancement; the gate propagates to the `carve` binary and e2e test.

Acceptance: `carve refresh` on this repo populates `headers`; editing a header invalidates only owning actions; refresh stays idempotent; unit + e2e tests cover header population, reuse, and missing-header caching.

### M2 - Complete the de-Bazel quirk inventory (CARVE_DESIGN §4.3)
Independent of M1; can run in parallel. One golden test per quirk (design §9.2).

Done (golden-tested in `carve/command`, wired through `refresh`):
- ✅ `-fno-canonical-system-headers` strip (clangd#1004) and `-gcc-toolchain` strip (clangd#1248).
- ✅ ccache wrapper: drop a leading `ccache` so argv[0] is the real compiler.
- ✅ MSVC `/showIncludes`[`:user`] strip; `-fmodules-cache-path=bazel-out/...` strip.
- ✅ Apple `wrapped_clang` + `__BAZEL_XCODE_*`: `command::ResolveXcodePlaceholders` substitutes the developer-dir/SDK paths; `refresh` invokes an injected `XcodeResolver` (the binary resolves via `xcode-select`/`xcrun` on macOS) only when a command carries a placeholder.
- ✅ `parse_headers` actions are filtered implicitly: their "source" is a header, so `FindSource` finds no TU and the action is skipped.

- ✅ **Execroot/absolute-path canonicalization of the persisted sidecar (the
  cross-host determinism property, §9).** scan-deps resolves generated and external
  headers to absolute, per-host cache paths (`.../execroot/_main/...`);
  `command::RelativizeToExecroot` rewrites them execroot-relative at storage
  (`refresh::ScanHeaders`, `shard::BuildShard`), and `CachedScanIsStale` resolves
  them back against the execroot to stat. The sidecar and header index now hold
  **zero** absolute paths (verified end-to-end on this repo: ~15k → 0) - so they are
  byte-identical across machines and remote-cache-shareable. The CDB `file` is also
  emitted execroot-relative (clangd resolves it against `directory`=execroot, the
  same path as before). A property test asserts the sidecar has no absolute paths.

Remaining (deferred - niche toolchains or a design-level property; best validated against M3's corpus):
- ⬜ Full *CDB* workspace-relative rewriting (`directory`=workspace root + the
  `//external` symlink choreography, §10) so the emitted database is itself
  relocatable. The CDB stays execroot-rooted until then (clangd consumes it
  correctly); this is the separate, larger change. Windows junctions are out of scope.
- ⬜ NVCC→clang flag translation (CUDA); Emscripten driver indirection; Windows command-line-length param-file specifics (carve already expands `@param` files via aquery `--include_param_files`).

Acceptance: golden test per quirk; platform-specific ones skipped where the toolchain is absent.

### M3 - Differential harness + clangd validation ✅
Correctness gate before building Layer B/C on top.

- ✅ `tools/cdb_diff.py`: a normalizing CDB differ (keys by workspace-relative source, ignores volatile output/dep/seed tokens), with a `--selftest` in CI. Diffs carve's CDB against any reference (e.g. a Hedron CDB).
- ✅ clangd verified working on carve itself: `clangd --check` builds the preamble/AST/index with zero compilation diagnostics using a toolchain-matched clang 22 (`docs/differential-report.md`).
- ✅ carve-vs-Hedron differences explained in the report (scan-deps vs `clang -M`, sidecar incrementality, header index, execroot-absolute vs workspace-relative paths). A live Hedron diff just needs a Hedron CDB pointed at `cdb_diff.py`; Hedron is intentionally not wired into carve (the tool it replaces) - a corpus run over external repos remains as optional follow-up.

### M4 - Layer B: `carve_refresh` rule ✅
`bazel run //:refresh` as the CDB entry point (design §4.6).

- ✅ **Nested-bazel concern resolved: a `bazel run` target, not a build action.** carve invokes `bazel aquery`/`bazel info`; spawning bazel inside a build action is the nested-bazel trap (lock/server contention, sandboxing). `carve_refresh` (`rules/carve.bzl`) generates a runfiles-aware launcher that runs the carve binary *after* the outer build and writes the CDB to `$BUILD_WORKSPACE_DIRECTORY`.
- ✅ Dogfooded: `bazel run //:refresh` writes the repo's `compile_commands.json` (22 entries). Analysis-tested (`rules/carve_test.bzl`): the target is runnable and carries the carve binary in its runfiles (no nested bazel in the test).

(The design's literal `bazel build //:compile_commands` shape is not viable for the reason above; the README/usage now shows `bazel run //:refresh`.)

### M5 - Layer C: aspect + shards
Per-action, individually-cacheable shards for huge repos (design §4.7).

- ✅ `aggregate` subcommand (`carve/aggregate`): the offline merge half - unions
  independently-produced sidecars (de-dup by (project_id, action_key), keep
  most-recently-written, deterministic order) and emits one CDB without running
  `bazel aquery`. Shares `refresh::EntriesFromRecords`.
- ✅ `shard` subcommand (`carve/shard`): the per-action invocation the aspect
  schedules - `carve shard --action_key=… --command_file=… --source=… --out=…`
  de-Bazels the command, resolves Xcode placeholders, scans headers (failed scan
  → unstamped, re-scanned later), and writes a one-record shard whose shape
  matches `refresh`. Verified end-to-end: `shard` → `aggregate` → valid CDB.
- ✅ The emitting aspect (`rules/cc_carve_aspect.bzl`): `cc_carve_aspect` walks
  the cc graph and schedules one cacheable `carve_shard` build action per compile
  action, reading the fully-expanded command from `action.argv` (no `@param-file`
  indirection - Bazel expands it). Shards land in the `carve_shards` output group.
  `carve_aspect_refresh` (`rules/carve.bzl`) is the `bazel run` driver: it builds
  the shards, then aggregates them against the real execroot.
- ✅ Per-action invalidation via Bazel: a shard's content is a function of its
  compile command, so the shard action's only input is its `command_file`. Bazel
  re-runs only the shards whose command changed; content edits don't re-shard
  (the entry is unchanged). Shards are not header-scanned - the lean `carve_shard`
  tool links no scanner; the database does not use headers and Bazel owns
  invalidation. Validated in CI by an analysis test (the wiring) and a `build_test`
  that builds the whole Layer C path with the lean tools (no LLVM build).

Acceptance: aspect emits shards (done); `carve aggregate` merges them (done);
a compile-command change re-shards only the affected action (done - via Bazel's
action cache on `command_file`). **M5 complete.**

- ✅ **Lean, LLVM-free Layer C tools:** the aspect's per-action exec tool is the
  scan-free `//carve:carve_shard`, and `carve_aspect_refresh`'s merge tool is
  `//carve:carve_aggregate`. Neither links `scan_deps`/LLVM - sharding records the
  command, aggregation merges protos, and neither needs a compiler. Building the
  whole Layer C path is now LLVM-free (≈seconds, not the tens-of-minutes exec-config
  LLVM build the full `carve` required), so the `build_test` rejoined CI.

- ✅ **First-party scoping (`exclude_external_sources`):** the aspect shards only
  main-repo compile actions by default, skipping external-repo targets (clangd
  resolves their headers via first-party entries' `-I` flags, so they need no
  entries). It is an aspect parameter on `carve_aspect_refresh`, default `True`;
  set `False` to shard the whole transitive graph.

- ✅ **Layer C header recording (`ASPECT_M`):** opt-in `record_headers` aspect
  parameter, default off so shards stay build-free. When set, each shard consumes
  its compile action's own `-MF .../x.d` dependency file -- robust reuse of the
  make-format `-M` output Bazel already generates for include validation (re-running
  the *wrapped* compiler standalone with `-M` proved unreliable, so we reuse the
  real depfile) -- and `carve_shard` parses it (lean, no LLVM, via the lifted
  `command::ParseMakeDependencies`), storing the exact `#include` set
  execroot-relative as `source_kind = ASPECT_M`. Enabling it couples sharding to
  building each TU (the `.d` is a compile byproduct). Validated end-to-end against
  an external consumer.

- ✅ **`aggregate` builds + persists a `HeaderIndex`:** after merging the shard
  records, `aggregate` builds the header -> owning-`action_key` index from their
  recorded `headers` (reusing the shared `sidecar::BuildHeaderIndex`, no logic
  duplicated) and writes it to `headers-index.binpb` next to the output database -
  the same filename and placement `refresh` uses beside its sidecar. A header owned
  by several actions lists all owners (lex-min canonical owner first); the
  empty-headers case (`record_headers` off) still writes the index, matching
  refresh. This closes Layer C's header-index parity with `refresh`.

### M6 - 0.1 release + distribution
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
  `.bazelrc`, which does NOT propagate to consumers. So a consumer building at
  `-std=c++23` would fail to compile `@llvm-project`.
  - **A build-graph config transition does NOT work (investigated + ruled out).**
    A `cxxopt`-appending transition on `@llvm-project//clang:tooling` over-reaches:
    its subgraph includes ~254 libc++/compiler-rt/libunwind targets (the
    from-source runtimes, which need C++20+ for `std::ranges`), and a `cxxopt`
    transition is global to the subgraph - it cannot do the *path-based* scoping
    (llvm/clang only, excluding the runtimes) that the `per_file_copt` does. So the
    C++17 scoping must stay a path-matched `per_file_copt`.
  - **Therefore the fix is to ship the flags as a consumer `.bazelrc` fragment**
    (the C++17 LLVM `per_file_copt`, the `scan_deps` `-fno-rtti`, and a C++23
    scoping for carve's own sources) for consumers to copy/import, the same shape
    mbo ships `llvm.MODULE.bazel`. The exact regexes depend on the consumer's
    external-repo path form, so author + validate the fragment against a real
    consumer workspace (or the `.bcr/presubmit.yml` build of `//carve:carve`) when
    cutting the release.
- ⬜ **Fully-hermetic runtimes (deferred enhancement, not a blocker).** The mixed
  C++17/C++23 build is ABI-safe today because each build links one consistent
  libc++ (CARVE_DESIGN §4.2 "Mixed C++ standards"). But on macOS that libc++ is the
  host Command Line Tools SDK's, not version-matched to the from-source LLVM. Building
  libc++/libc++abi/libunwind from the `@llvm-project` source on every platform would
  make the build fully hermetic + the ABI safety airtight. hermetic-llvm's `osx`
  extension has no from-source-libc++ knob, so this means forking/extending the
  toolchain module + heavy rebuilds - a significant effort, deferred until there is
  a concrete need.

Acceptance: a bzlmod consumer can `bazel_dep(name = "helly25_carve")` and get a working CDB; tagged release.

## Cross-cutting / parallelizable
- ✅ `prune` subcommand (`carve/prune`): GC sidecar rows whose `written_at` is older than `--prune_after_days`; unstamped rows kept. `carve prune --sidecar=... --prune_after_days=30`.
- ✅ `aggregate` subcommand (`carve/aggregate`): merges independently-produced
  sidecars into one CDB offline. ✅ `shard` subcommand (`carve/shard`): builds one
  per-action shard. ✅ `cc_carve_aspect` + `carve_aspect_refresh` (Layer C): the
  aspect schedules `shard` per compile action and aggregates the shards.
- Parallel scan-deps (`--jobs`) - fold into M1.
- Property tests: ✅ cross-host determinism - `refresh_test` asserts the sidecar holds no absolute paths after a refresh whose scanner returns execroot-absolute headers (M2 canonicalization). Idempotency (refresh twice → identical sidecar) still to be codified beyond the dogfood check.
- Keep [docs/test-plan.md](test-plan.md) at zero open debts.
- Latency reality: Layers A/B re-run `bazel aquery` every refresh (design §3.1); sub-second incrementality on huge repos is a Layer C (M5) property - don't over-promise before then.

## Sequencing rationale
M1 first - it converts the working skeleton into the actual product (incrementality + headers). M2 runs alongside (independent). M3 validates correctness before B/C are layered on. M4 → M5 (B before C, per the design's layering). Release (M6) last. Riskiest dependency (LLVM linkage) and toolchain are already done.
