# Changelog

All notable changes to this project will be documented in this file. Format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versioning
follows [SemVer](https://semver.org/).

## [Unreleased]

- Initial bootstrap (MODULE.bazel, .bazelrc, design doc).
- Design review: correct the scan-deps LLVM-libs linkage assumption
  (toolchains_llvm is not a linkable libs source), front-load it as a month-1
  spike, and document decoupling Layer A from scan-deps; clarify Layer A/B
  latency vs. Layer C; add execroot path-canonicalization quirk and the
  determinism precondition; note action_key churn; reframe aquery proto
  vendoring as a maintenance task; fix dangling doc references.
- Testing convention: write relevant tests at all levels; treat one-shot
  experiments as planning input for real tests, never as a substitute.
- Align design with helly25 house conventions: `carve/<module>/` package layout,
  `namespace carve::<module>`, `_cc`/`_test` target naming, `deps` vs
  `implementation_deps`; add RULES.md (code style) referenced from AGENTS.md.
- `carve` binary skeleton: `//carve:carve` with `absl::Flags` subcommand
  dispatch (`refresh`/`aggregate`/`shard`/`prune`), `carve/cli` module +
  `cli_test`, `//:carve` run alias. Handlers return `UnimplementedError` until
  their modules land.
- Scope `-Werror` to first-party code via `--per_file_copt=//carve/.*`, so
  third-party deps (re2 vs. a newer Abseil deprecation) keep their own posture.
- Add `docs/test-plan.md` ledger for manually-verified, not-yet-codified
  behavior (currently: the binary's exit-code mapping).
- `carve/cdb` module: `CompileCommand` model, deterministic JSON Compilation
  Database serialization (`ToJson`), and atomic write (`WriteAtomically` /
  `Write`) via temp-file + rename. Unit-tested incl. escaping and overwrite.
- `carve/command` module: IO-free `DeBazel` argv transform dropping flags
  clangd cannot consume (`-fno-canonical-system-headers`,
  `-gcc-toolchain`/`--gcc-toolchain` and the `=`-joined form). First slice of
  the de-Bazeling quirk inventory (CARVE_DESIGN.md section 4.3).
- Vendor a trimmed `analysis_v2.proto` (Bazel 9.1.0, cquery messages + the
  `build.proto` import removed) at `carve/third_party/bazel`, and add the
  `carve/aquery` module: `ParseCompileActions` decodes an
  `ActionGraphContainer` and extracts compile actions (CppCompile/ObjcCompile/
  CppModuleCompile) with path-fragment-resolved primary outputs. Unit-tested.
- `carve/refresh` module: `BuildEntries` orchestrates aquery -> de-Bazel ->
  source detection into `cdb::CompileCommand` entries (the Layer A pipeline,
  minus scan-deps and execroot path resolution). Unit-tested.
- Wire `carve refresh --aquery_proto=FILE [--output=PATH] [--directory=DIR]`:
  `refresh::RunRefresh` reads a captured aquery proto and atomically writes the
  CDB. This is a runnable Layer A path (in-process aquery and scan-deps land
  later). Verified end-to-end against carve's own `bazel aquery` output.
- `carve/aquery`: expand response files (`@path`) inline from the action's
  embedded param files (`bazel aquery --include_param_files`), iteratively and
  cycle-guarded; unmatched `@` tokens pass through verbatim.
- Adopt pre-commit (`.pre-commit-config.yaml`): standard hygiene hooks,
  buildifier format+lint, and pygrep guards (blocked-merge markers; TODO/FIXME
  must cite an issue URL or email). Reformat BUILD files to buildifier canon.
- `carve/refresh`: emit absolute `file` paths (resolved against the entry's
  `directory`/execroot) so clangd matches sources unambiguously; absolute or
  directory-less paths pass through unchanged.
- Extract `carve/io` (atomic write + read), shared by `cdb` and `refresh`.
- `carve/sidecar`: Edition 2024 `carve.proto` schema (ActionRecord/
  ActionRecords/HeaderOwners/HeaderIndex; `primary_output` added) and
  `Load`/`Save` (binary proto, atomic, missing-file => empty), `DiffActionKeys`,
  and `MergeRecords` (keeps a stored record when key+command match, preserving
  cached fields; rebuilds on change; drops removed). Unit-tested.
- `carve/refresh`: records-based pipeline backed by the sidecar. `RunRefresh`
  loads the sidecar, merges the current actions (reusing unchanged records),
  writes the sidecar back, and emits the CDB from the merged set; `--sidecar`
  flag (default `.carve-cache/entries-by-actionkey.binpb`, empty disables).
  Verified idempotent end-to-end (CDB and sidecar byte-stable across runs).
- Project-scoped merge (design goal #3): records carry a `project_id`,
  `MergeRecords` only replaces the refreshing project's rows and leaves other
  projects untouched, and the emitted CDB combines all projects. `--project_id`
  flag. Verified end-to-end: refreshing project A then B yields a combined CDB,
  and re-refreshing A does not clobber B.
- `carve/process`: POSIX subprocess runner (`Run`) capturing stdout/stderr
  concurrently (poll, no deadlock). mbo-upstream candidate.
- In-process aquery: `carve refresh --targets=PATTERN [--bazel=PATH]` runs
  `bazel aquery --output=proto --include_param_files` itself (no pre-captured
  proto needed); `--aquery_proto` still overrides. `--targets` defaults to
  `//...`. This is the real Layer A entry point. Dogfooded against this repo.
- Default the CDB `directory` to `bazel info execution_root` when `--directory`
  is unset, so exec-relative argv resolve for clangd (entries are now absolute
  under the execroot). Dogfooded.
- `//carve/e2e:end_to_end_test`: drives the built `carve` binary (via
  `carve/process`) against a generated aquery proto and asserts the produced
  CDB plus the exit-code contract (missing/unknown subcommand => 2, failed
  refresh => 1). A fake-`bazel` stub also covers the in-process `--targets`
  path hermetically. `docs/test-plan.md` is now empty of open debts.
- CI: `.github/workflows/main.yml` — pre-commit job, `bazel test --config=clang
  //...` on ubuntu + macOS, and an aggregating `done` gate. Add pre-commit
  hooks for managed buildifier and actionlint (workflows are actionlint-clean).
- Bump the toolchain to LLVM 22.1.7 (via a `toolchains_llvm` local override on
  the released 1.7.0 — shas from its `toolchain/distributions/github.jsonc`),
  whose headers are C++23-clean (no more `std::aligned_union_t`), so no
  deprecation suppression is needed. macOS-x86_64 omitted (no upstream build).
- **scan-deps (resolves the §4.2 linkage risk):** `carve/scan_deps` runs
  clang's in-process `DependencyScanningTool` over a compile command and returns
  its header set. Libs come from a same-version prebuilt LLVM `http_archive`
  (`libclang-cpp` + headers, `-isystem`), gated to darwin-arm64 while proven on
  one platform. Unit-tested (header scan + missing-header error).
- Warning policy (`.bazelrc`): `external_include_paths` so third-party headers
  (e.g. googletest under clang 22) don't trip our first-party `-Werror`; and
  `-fno-rtti` for the `scan_deps` subtree to match LLVM's no-RTTI build.
- Single LLVM source: drop the separate libs `http_archive` and link scan-deps
  against `clang_cpp` (libclang-cpp + headers) exposed by the toolchain's own
  distribution repo — one download, guaranteed ABI match. The `clang_cpp`
  target is added to `toolchains_llvm`'s `BUILD.llvm_repo.tpl` (pending upstream)
  and consumed via a `local_path_override` to the sibling checkout until a
  release ships it. Bump LLVM 22.1.7 -> 22.1.8 and helly25_mbo 0.10.0 -> 0.11.1.
- Pin the macOS deployment target to 11.0 so libc++ exposes `std::filesystem`.
- **Switch the LLVM toolchain and clang libraries to hermetic-llvm** (the `llvm`
  BCR module, LLVM 22.1.7): drop `toolchains_llvm`, the `clang_cpp` fork target,
  and the `local_path_override`. `carve/scan_deps` now links
  `@llvm-project//clang:tooling` built from source with the same libc++ as our
  C++23 code — no shared-library ABI boundary and no libstdc++-on-Linux coupling.
  LLVM's own sources build at C++17 (scoped in `.bazelrc`); ours stay C++23.
- `carve/scan_deps` is no longer gated to darwin-arm64 — enabled on macOS and
  Linux, built and tested under the hermetic toolchain in CI.
- Remove the orphaned `third_party/llvm/` overlay (the obsolete prebuilt-libs
  BUILD overlay) and correct the now-stale `--config=clang` comment in
  `.bazelrc`, finishing the hermetic-llvm switch cleanup.
- Add `docs/IMPLEMENTATION_PLAN.md`: current status snapshot and a
  dependency-ordered milestone breakdown (M1 wire scan-deps into refresh →
  M6 release), superseding the design's month-by-month sketch (§11).
- Vertically align all Markdown tables and enforce it: add
  `tools/align_md_tables.py` and an `align-md-tables` pre-commit hook
  (fence-aware, idempotent, preserves alignment markers).
- M1 keystone: `carve/refresh` populates each record's `headers` via an injected
  `HeaderScanner` (DI so `refresh` stays cross-platform and unit-testable with a
  fake); the `carve` binary wires the real `scan_deps::ScanDependencies`, whose
  linux+macos gate propagates to the binary and e2e test. Dogfooded:
  `carve refresh --targets=//carve/cdb:cdb_cc` captures the source's headers
  into the sidecar via in-process scan-deps.
- Incremental scan: `refresh` now scans only added/changed actions and reuses
  the cached headers of unchanged ones (`sidecar::FindReusableRecord` query),
  instead of re-scanning every action each run. Dogfooded: a warm re-refresh is
  ~3x faster than cold (scan skipped).
- `written_at` stamping: `refresh` stamps each of its project's records with the
  current unix time (added, changed, and reused alike) via an injected `Clock`
  (DI for deterministic tests; the `carve` binary uses the wall clock). This
  records project liveness for the upcoming `prune` GC; other projects' rows keep
  their own timestamps.
- Persist the `HeaderIndex`: `refresh` writes `headers-index.binpb` next to the
  entries sidecar (the design's second cache file), a deterministic header ->
  owning-action reverse index rebuilt from the merged records each run. New
  `sidecar::LoadHeaderIndex`/`SaveHeaderIndex`. Dogfooded: maps 1187 headers for
  `//carve/cdb:cdb_cc` and is byte-stable across runs.
- Header-staleness invalidation: `refresh` re-scans an otherwise-unchanged action
  when any of its cached headers (or its source) was modified on disk since the
  scan was recorded (`written_at`), so editing a header invalidates only the
  actions that include it. `MergeRecords` takes a `rescanned` key set so a fresh
  scan wins over the stale cache; `sidecar::HasMatchingRecord` becomes
  `FindReusableRecord` (returns the matched record so its `written_at`/headers can
  be checked). Granularity is one second. Dogfooded: a warm refresh stays on the
  fast reuse path until a header is touched, which returns that action to a
  cold-cost re-scan.
- Missing-generated-header guard: when scan-deps cannot resolve an action's
  headers (typically an unbuilt generated header), `refresh` emits the CDB entry
  but leaves that record unstamped (`written_at` unset) so the next refresh
  re-scans it instead of caching the incomplete set (CARVE_DESIGN §4.2).
  `RunRefresh` now returns a `RefreshStats` (`entries`/`scanned`/`reused`/
  `unresolved`); the `carve` binary prints a one-line summary and an
  unresolved-headers warning. Dogfooded: `wrote 1 entries (0 scanned, 1 reused)`
  on a warm run.
- Parallel scan-deps (`--jobs`, default hardware concurrency): `refresh` scans
  added/changed actions across a worker pool (the scan *decision* stays serial,
  so the sidecar is deterministic). The pool is a small `absl::Mutex`-guarded
  type with full thread-safety annotations, enforced at compile time by
  `-Wthread-safety` (first-party `-Werror`). (A runtime tsan CI job is not wired
  up: the hermetic `llvm` toolchain cannot build the compiler-rt sanitizer
  runtime; see the commented-out `:tsan` config in `.bazelrc`.) Completes M1.
- M2 de-Bazel quirks: `command::DeBazel` now also drops a leading `ccache`
  wrapper, MSVC `/showIncludes`[`:user`], and `-fmodules-cache-path=bazel-out/...`;
  new `command::ResolveXcodePlaceholders` substitutes Apple `wrapped_clang`
  `__BAZEL_XCODE_*` placeholders, which `refresh` applies via an injected
  `XcodeResolver` (the binary resolves `xcode-select`/`xcrun` on macOS, only when
  a command carries a placeholder). Golden-tested. (nvcc/emscripten flag
  translation and workspace-relative path canonicalization remain.)
- M3 differential validation: `tools/cdb_diff.py` (a normalizing compilation-
  database differ, `--selftest` in CI) and `docs/differential-report.md` (clangd
  consumes carve's CDB cleanly with a toolchain-matched clang 22; the
  missing-generated-header guard observed on a clean tree; carve-vs-Hedron
  differences explained).
- M4 Layer B: the `carve_refresh` rule (`rules/carve.bzl`) and a `//:refresh`
  entry point — `bazel run //:refresh` writes `compile_commands.json` to the
  workspace root. It is a `bazel run` target, not a build action (carve runs
  `bazel aquery`; nesting bazel in an action is the trap). Dogfooded;
  analysis-tested.
- `prune` subcommand (`carve/prune`): `carve prune --sidecar=... --prune_after_days=N`
  drops sidecar records not refreshed within N days (GC by `written_at`;
  unstamped records are kept), rewriting the sidecar only when something changed.
  Unit-tested and dogfooded; de-stubs the `prune` CLI command.
- `carve/sidecar`: `BuildHeaderIndex` builds the deterministic header ->
  owning-action index (owners sorted, lex-min canonical) from action records —
  the basis for header-driven incremental invalidation (M1; CARVE_DESIGN §4.5).
  sidecar tests now build proto data from `ParseTextProtoOrDie`/`EqualsProto`
  text literals instead of imperative setters.
- CI: persist Bazel disk + repository caches across runs (`actions/cache` over
  `--disk_cache`/`--repository_cache`; the from-source LLVM build is ~12 min cold,
  cacheable), and make the `done` gate self-check that every workflow job is
  wired into its `needs` (mirrors mbo/bzl/xff).
