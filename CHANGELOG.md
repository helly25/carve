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
- Pin the macOS deployment target to 14.0 (a current floor for this developer
  tool), set on every compile via `--copt` (so the C deps zlib/utf8_range are
  covered, not just C++) and on the link, so all objects agree on the minimum OS
  and `ld64.lld` emits no "object ... newer than target minimum" warnings.
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
- `aggregate` subcommand (`carve/aggregate`): `carve aggregate --sidecars=a,b,...
  --output=compile_commands.json --directory=<execroot>` merges independently-
  produced sidecars (e.g. parallel build shards of one workspace) into one
  compilation database offline (no `bazel aquery`). `Combine` unions records,
  de-duplicates by (project_id, action_key) keeping the most-recently-written on
  collision, and sorts deterministically; a missing sidecar contributes nothing.
  Shares the records->CDB projection with `refresh` (`EntriesFromRecords` lifted
  to `carve/refresh`'s public API). Unit-tested; de-stubs the `aggregate` CLI
  command (the per-action aspect that emits the shards remains M5/Layer C).
- `shard` subcommand (`carve/shard`): `carve shard --action_key=... --command_file=...
  --source=... --out=...` builds the one-record shard for a single compile action —
  the per-action invocation the Layer C aspect schedules (CARVE_DESIGN §4.7). It
  de-Bazels the command, resolves Apple `wrapped_clang` placeholders, scans headers
  via the injected scanner (a failed scan records none and leaves the row unstamped
  so it is re-scanned), and stamps `written_at` on a complete scan. The record shape
  matches `refresh`, so shards merge cleanly via `aggregate`. Verified end-to-end:
  `shard` -> `aggregate` produces a valid CDB. Unit-tested; de-stubs the `shard` CLI
  command. The emitting Starlark aspect (the remaining half of Layer C) is next.
- M5 Layer C: the emitting aspect (`rules/cc_carve_aspect.bzl`) and
  `carve_aspect_refresh` (`rules/carve.bzl`). `cc_carve_aspect` walks the cc graph
  and schedules one cacheable `carve shard` build action per compile action —
  reading the fully-expanded command straight from `action.argv` (Bazel already
  expands param files) — collected in the `carve_shards` output group.
  `carve_aspect_refresh` is a `bazel run` target that builds those shards, then
  aggregates them into `compile_commands.json` against the real execroot. A
  shard's content is a function of its compile command alone, so its one input is
  the `command_file`: Bazel re-runs only the shards whose command changed (a new
  flag/define/dep) — the per-action incrementality Layers A/B cannot offer
  (CARVE_DESIGN §4.7). Editing source/header *content* leaves the command (and the
  database entry) unchanged, so no re-shard is needed. Shards are therefore not
  header-scanned (`carve shard --scan=false`): the database does not use headers,
  and delegating invalidation to Bazel avoids scanning every TU in a build action.
  Verified: the aspect shards a leaf target and (as a stress test) the entire LLVM
  graph cleanly; `shard` -> `aggregate` yields a valid CDB. CI runs an analysis test
  for the wiring; a `manual` `build_test` actually builds a shard on demand (it is
  not in the default suite because the aspect's `carve shard` tool needs an
  exec-config carve = a full from-source LLVM build, too costly per CI run — the
  shard data path itself is covered by `shard_test`). **M5 (the full Layer C data
  path: aspect + shard + aggregate) is complete.**
- `carve shard --scan` flag (default `true`): standalone `shard` scans headers as
  before; the Layer C aspect opts out with `--scan=false`.
- Adopt the helly25 house C++ style guide as `STYLE_CPP.md` (mirrors `mbo`/`xff`,
  adapted to C++23 and the `CARVE_` macro prefix; macros consumed from the `mbo`
  dependency keep their `MBO_` prefix). It is the detailed companion to `RULES.md`
  (idioms, error handling, output/`AbslStringify`, concurrency, protobuf,
  GoogleTest) and is referenced from `RULES.md` and `AGENTS.md`. carve's
  `.clang-format` and `.clang-tidy` already match mbo's.
- Adopt the Hedron compile-commands extractor (helly25 fork) as a dev module in
  `bazelmod/dev.MODULE.bazel` (`include`d from `MODULE.bazel`, all `dev_dependency`
  so nothing propagates to dependents), mirroring mbo/xff. carve adds its own
  `//:refresh_compile_commands` target (scoped to `//carve/...` with `--config=clang`
  so the captured commands use the hermetic toolchain); `bazel run
  //:refresh_compile_commands` writes a `compile_commands.json` for clangd and local
  clang-tidy. (carve replaces Hedron, but uses it for its own dev loop until it can
  self-host.) Also pulls in `depend_on_what_you_use`, and bumps `rules_cc` to 0.2.19
  to match the dev graph.
- Apply clang-tidy across the first-party sources. Fixes: replace the non-thread-safe
  `std::strerror` with `absl::ErrnoToStatus` (`concurrency-mt-unsafe`); name magic numbers;
  rename too-short identifiers; `std::array` instead of a C array; drop an unused include
  and add `<cstddef>`; unsigned bit operations. `NOLINT`-with-reason the genuinely-intentional
  cases: the `execvp` `const_cast`, `pipe()` (macOS has no `pipe2`; the child closes every fd
  before exec), the POSIX-poll-flag bitmask, and `AppendKey`'s two `string_view` params. The
  systematic stylistic findings (`operator[]` bounds checks, file-local anonymous namespaces)
  and the misconfiguration noise (`llvm-header-guard` wanting absolute-path guards; system
  symbols `misc-include-cleaner` wants internal SDK headers for) are left in place - the checks
  stay enabled, carve just does not chase them yet. (clang-tidy is run with a real, version-
  matched clang-tidy; the hermetic `llvm`-minimal toolchain ships none, and a dependency's bare
  `version` file shadows libc++ `<version>` unless its include dir is prepended.)
- M6 release scaffolding (source-only, mirrors the helly25 house pattern): `.bcr/`
  metadata (`metadata.template.json`, `source.template.json`, `presubmit.yml`),
  `.github/workflows/release.yml` (a numeric-semver tag drives the `bazel-contrib`
  release ruleset) + `publish.yaml` (mirror to the Bazel Central Registry) +
  `release_prep.sh` (validates the tag matches `MODULE.bazel` and the CHANGELOG,
  emits an empty root `BUILD.bazel`, comments the dev `include`, excludes dev-only
  paths, builds the source tarball, and prints the Keep-a-Changelog release notes),
  plus `tools/header.txt`. README/IMPLEMENTATION_PLAN updated. Nothing publishes
  until a tag is pushed; prebuilt binaries are out of scope (source-only). Known
  gap before a real release: carve's C++17 scoping for the from-source LLVM is in
  `.bazelrc` and does not propagate to consumers, so `//carve:carve` may not build
  for a bzlmod consumer until that moves into the BUILD graph.
- `carve/sidecar`: `BuildHeaderIndex` builds the deterministic header ->
  owning-action index (owners sorted, lex-min canonical) from action records —
  the basis for header-driven incremental invalidation (M1; CARVE_DESIGN §4.5).
  sidecar tests now build proto data from `ParseTextProtoOrDie`/`EqualsProto`
  text literals instead of imperative setters.
- CI: persist Bazel disk + repository caches across runs (`actions/cache` over
  `--disk_cache`/`--repository_cache`; the from-source LLVM build is ~12 min cold,
  cacheable), and make the `done` gate self-check that every workflow job is
  wired into its `needs` (mirrors mbo/bzl/xff).
- Layer C runs lean, LLVM-free tools: split `//carve:carve_shard` (the aspect's
  scan-free per-action exec tool) and `//carve:carve_aggregate` (the launcher's
  proto-merge tool) out of the full `carve` binary. Neither links `scan_deps` or
  the from-source LLVM, so building the whole Layer C path no longer triggers an
  LLVM compile, and the now-cheap `aspect_shards_build_test` rejoined CI.
- Layer C aspect scopes to first-party by default: `cc_carve_aspect` /
  `carve_aspect_refresh` now take `exclude_external_sources` (default `True`),
  which skips sharding external-repo compile actions (clangd resolves their
  headers via first-party entries' `-I` flags). Set `False` to shard the whole
  transitive graph.
- Sanitizer CI (ubuntu): asan+ubsan and tsan jobs run `--config=asan
  --config=ubsan` / `--config=tsan` over `//carve/...`. This required patching the
  BCR `llvm` module's compiler-rt so its sanitizer runtimes build at all (the
  internal symbolizer couldn't see libc++ on macOS nor LLVM's public/generated
  headers on either platform; see `bazelmod/patches/` + `single_version_override`
  in MODULE.bazel, upstreamed via hermeticbuild/hermetic-llvm#655 and a follow-up).
  LLVM-linking targets (`//carve:carve`, scan_deps, e2e) are tagged `no_san` and
  excluded. macOS sanitizers are deferred (the toolchain wires the runtimes +
  interface headers for Linux only).
- Cross-host determinism (CARVE_DESIGN.md §9): scan-deps resolves generated and
  external headers to absolute, per-host Bazel cache paths
  (`.../execroot/_main/...`). `command::RelativizeToExecroot` now stores them
  execroot-relative — as the sources and argv already are — so the persisted
  sidecar and header index hold **zero** absolute paths (verified end-to-end on
  this repo: ~15k → 0) and are byte-identical across machines and
  remote-cache-shareable. The CDB `file` is likewise emitted execroot-relative
  (clangd resolves it against `directory`=execroot, the same path as before). A
  `refresh_test` property test asserts the sidecar carries no absolute paths.
  (IMPLEMENTATION_PLAN.md M2; the full CDB workspace-relative rewrite —
  `directory`=workspace + `//external` symlink — remains a deferred follow-up.)
- Layer C header recording (M5, opt-in): a `record_headers` parameter on
  `carve_aspect_refresh` (default off, so shards stay build-free). When set, each
  shard consumes its compile action's own `.d` dependency file and `carve_shard`
  parses it (lean, no LLVM) into the exact `#include` set, stored execroot-relative
  as `source_kind = ASPECT_M`; the make-format parser moved to
  `command::ParseMakeDependencies` (shared with `scan_deps`). Enabling it builds the
  TUs (the `.d` is a compile byproduct); re-running the wrapped compiler standalone
  with `-M` proved unreliable, so the real depfile is reused. Validated end-to-end
  against an external consumer.
- Enforce the docs chain (CLAUDE.md -> AGENTS.md -> RULES.md/STYLE_CPP.md): the
  AI-agent checklist now mandates reading RULES.md and STYLE_CPP.md before writing
  C++, so the detailed style (status matchers, `// NL`, idioms) is in the compulsory
  pre-coding read rather than only cross-referenced from the document map.
