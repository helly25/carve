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
