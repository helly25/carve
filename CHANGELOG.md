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
- CI: persist Bazel disk + repository caches across runs (`actions/cache` over
  `--disk_cache`/`--repository_cache`; the from-source LLVM build is ~12 min cold,
  cacheable), and make the `done` gate self-check that every workflow job is
  wired into its `needs` (mirrors mbo/bzl/xff).
