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
