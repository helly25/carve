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
