# Clean-Slate Rewrite

Design for replacing this tool with a from-scratch implementation under permissive licensing. The alternative path — extending the current Python codebase incrementally — was documented separately in the originating fork's `INCREMENTAL_DESIGN.md` and is not carried into this clean-slate repo. This document assumes the rewrite path was chosen and focuses on what to build.

## 1. Goals

1. Produce a `compile_commands.json` for clangd from a Bazel build graph, correctly de-Bazeled so clangd can introspect it without Bazel-specific environment.
2. Incremental refresh by default: edit one source, only that action's entry regenerates.
3. Native support for multiple projects writing into a shared CDB without clobbering each other.
4. Refresh latency on the order of seconds for typical edits on large monorepos. **Caveat:** Layers A/B re-run `bazel aquery` on every refresh and therefore pay the graph-analysis cost regardless of edit size; the sidecar only saves re-scanning unchanged actions, not the query. Sub-second incremental refresh on huge repos is a Layer C property (per-action Bazel action-cache invalidation), not a Layer A promise. See section 3.1.
5. Independent of the existing Hedron/helly25 codebase. No code derivation. Inventory of platform quirks is rederived from Bazel and clang source plus public issue history.
6. Permissive license (Apache-2.0 or MIT) chosen at project inception.

## 2. Hard constraints

- **Python 3.13.** Used only where Bazel forces it (test scripts, possibly bazel-genrule glue). Strong preference: zero Python in the main tool.
- **Bazel 9.1.** Bzlmod default. Modern `rules_cc`.
- **C++23**, **Clang 20.1+** as the build toolchain. C++23 features are in scope (`std::expected`, deducing-this, ranges enhancements, `std::print`/`std::format`).
- **GTest** for unit tests.
- **Abseil** for utilities (`absl::StatusOr`, `absl::flat_hash_*`, `absl::log`, `absl::Flags`, `absl::strings`).
- **mbo** ([helly25/mbo](https://github.com/helly25/mbo)) as the project-owned extension layer above Abseil. Brings the Google-internal utilities Abseil did not open-source plus useful additions we control. Bzlmod-ready. Supports clang compilation through `bazel-contrib/toolchain_llvm`, which matches our toolchain choice.
- **No Boost.** Anywhere.
- **No code copied from the existing codebase.** Insights and the quirk inventory are fine; specific expressions are not.

### 2.1 Library priority

When picking where a capability lives, prefer in this order:

1. **Standard C++23**, if usable out of the box.
2. **Abseil**, if usable out of the box.
3. **mbo**, as a wrapper or extension when std or Abseil need smoothing for our use. Anything mbo lacks that we need we add upstream there, not locally in `carve`. This keeps the dependency surface controllable: we own the wrapper layer.

`carve` itself should rarely define general-purpose utilities. New utility helpers default to a contribution to mbo unless they are unambiguously specific to compile-commands extraction.

## 3. Architecture overview

Three composable layers, shipped in order. Each is independently useful; later layers add incrementality and remote-cache friendliness.

```
+-------------------+        +-------------------+        +-------------------+
| Layer A           |  -->   | Layer B           |  -->   | Layer C           |
| Single-shot tool  |        | Bazel rule wraps  |        | Aspect produces   |
| bazel run         |        | tool as a build   |        | per-action shards |
|                   |        | action            |        | aggregator merges |
+-------------------+        +-------------------+        +-------------------+
```

- **Layer A.** A C++ binary, `carve`, runnable as `bazel run @carve//:refresh`. Internally: `bazel aquery` once, in-process scan-deps for header extraction, persistent sidecar cache, atomic CDB write.
- **Layer B.** A Bazel rule `cc_carve` that runs `carve` as a build action. CDB becomes `bazel build //:compile_commands` instead of `bazel run`. Matches the project's normal build muscle memory.
- **Layer C.** An aspect that registers per-action shard-producing actions. Shards are individually cacheable (locally and remotely). Aggregator merges them. Best for huge repos and shared remote caches.

Critical property: every layer above shares the same data model and persisted formats. Adding a layer does not invalidate caches from lower layers.

### 3.1 Where the latency actually goes

Be honest about the cost model, because it drives the layering:

- **`bazel aquery` runs on every Layer A/B refresh.** It re-runs analysis of the requested pattern. On a large monorepo this is tens of seconds to minutes and is *independent of how small the edit was*. This is the dominant cost of a refresh and the sidecar does not reduce it.
- **The sidecar reduces scan-deps work, not aquery work.** Its payoff is skipping header re-scans for actions whose command did not change. Valuable, but it does not deliver "seconds after a one-line edit" on its own.
- **Only Layer C escapes the full-graph query.** The aspect schedules per-action shard producers that Bazel's action cache invalidates individually, so an edit reruns work proportional to what changed. This is the path to the goal-4 latency on huge repos.

Practical consequence: the headline incrementality story is a Layer C deliverable. Layers A/B are "correct, shared-CDB-capable, and skip redundant scanning" — not "sub-second." Marketing and README copy should reflect this.

## 4. Component breakdown

### 4.1 `carve` (C++ binary)

The core tool. Single statically-linked binary. Subcommands:

```
carve refresh   [--output=PATH] [--project-id=ID] [--targets=PAT]   # Layer A entry point
carve aggregate [--shards=DIR] [--output=PATH] [--project-id=ID]    # Layer C entry point
carve shard     --action-key=KEY --command=FILE --source=PATH       # Layer C per-action invocation
carve prune     [--age=N]                                           # Sidecar GC
```

Internal modules:

| Module      | Responsibility                                                                               | Notable deps                    |
| ----------- | -------------------------------------------------------------------------------------------- | ------------------------------- |
| `aquery`    | Spawns `bazel aquery --output=proto` (binary), parses via linked `analysis_v2.proto`         | mbo subprocess helper (or std)  |
| `scan_deps` | In-process header dependency scan via `clang::tooling::dependencies::DependencyScanningTool` | clangDependencyScanning         |
| `command`   | Argv normalization and de-Bazeling patches (the quirk inventory)                             | `absl::strings`, mbo path utils |
| `sidecar`   | Persistent action-keyed cache and bi-directional header index                                | protobuf, mbo atomic-write      |
| `cdb`       | Atomic JSON output, merge semantics                                                          | mbo atomic-write                |
| `cli`       | `absl::Flags`-driven subcommand dispatch                                                     | `absl::Flags`                   |

Each module is a self-contained Bazel package following the helly25 house layout
(see [RULES.md](RULES.md)): `carve/<module>/` with `namespace carve::<module>`,
a `<module>_cc` library, and a colocated `<module>_test`. The binary entry point
is `//carve:carve` (with a `//:refresh` alias for the documented `bazel run`).

Per the library priority (section 2.1), where mbo already covers a need we use it; where it does not, we contribute the missing piece to mbo rather than rolling a local helper inside `carve`. Examples of expected contributions (subject to whatever mbo already ships):

- Subprocess wrapper with separate stdout/stderr capture, env override, and a configurable timeout.
- Atomic file write (write-temp-rename) usable for both proto-binary and JSON outputs.
- Workspace-root discovery and `BUILD_WORKSPACE_DIRECTORY` resolution.
- Cross-platform symlink/junction handling (matters for the `//external` choreography).

### 4.2 Scan-deps integration

Use `clang::tooling::dependencies::DependencyScanningService` and `DependencyScanningTool` from `clangDependencyScanning`. Key properties:

- **In-process.** Zero subprocess overhead per action.
- **Shared `DependencyScanningWorker` per thread.** Filesystem state cached across actions.
- **Module-aware.** If the project uses clang modules later, scan-deps already understands them.
- **Returns canonical `-MD`-style dependencies.** Same shape as the current preprocessor output, so the rest of the pipeline does not change with this swap.

Expected speedup over forking `clang -M` per action: 5x to 10x on large repos based on public benchmarks of `clang-scan-deps`.

#### Linkage reality (resolved)

`toolchains_llvm` provides a *compilation toolchain* — clang to compile **with**. It does **not** ship `libclangDependencyScanning` (or any LLVM/Clang library + headers) to link **against**. Linking `DependencyScanningTool` required choosing one of:

1. **Build llvm-project from source under Bazel.** Hermetic and correct, but a large/slow build that strains the "5-minute clone-to-working" goal in section 7.
2. **Hermetic prebuilt LLVM libs** (a repo rule exposing `cc_library` targets for the needed Clang/LLVM static libs). Adds a dependency we must name and pin, but inherits whatever STL the prebuilt was built with — on Linux that is libstdc++, which a libc++ tool cannot link against.
3. **Local-install bridge** (`bazel-llvm-bridge`, `@local_llvm//:llvm_headers`). Non-hermetic; breaks "works immediately after clone." Acceptable only as a dev fallback.

**Decision (implemented).** Option 1, via [hermeticbuild/hermetic-llvm](https://github.com/hermeticbuild/hermetic-llvm) — the `llvm` BCR module — which makes option 1 ergonomic: it provides *both* a hermetic clang toolchain (libc++ built from source on every platform) and the `@llvm-project` Bazel overlay. `//carve/scan_deps` links `@llvm-project//clang:tooling` (which pulls `:dependency_scanning`), built from source with the **same libc++** as our own code. That dissolves the ABI question: no shared-library boundary, no libstdc++-on-Linux coupling. An earlier iteration took a prebuilt path (a `clang_cpp` target added to `toolchains_llvm` exposing `libclang-cpp`); it worked on macOS but inherited the prebuilt's STL and so could not stay libc++ on Linux — hence the move to source-built libs. Cost: a ~12-min cold build of the LLVM/Clang subset, cacheable locally and remotely. carve's own code stays C++23; LLVM's own sources build at their native C++17 under one shared libc++ (scoped in `.bazelrc`).

**Optional optimization (deferred).** The sole downside of source-built libs is that ~12-min cold build. It caches cleanly, so caching (local disk cache, CI cache, optionally a shared/remote cache) is the first answer. If that build time becomes a recurring drag on developers, an optional — but maintenance-heavy — escalation is option 2 done *with libc++*: build a `libclang-cpp` + headers artifact once, host it (e.g. GitHub Releases), and link it via `http_archive`. Fast to consume, but it re-adds a per-LLVM-bump chore to rebuild and re-host the artifact, so it is not worth doing until caching proves insufficient. Note hermetic-llvm ships no prebuilt clang dev libraries today, and the only freely available `libclang-cpp` prebuilts (upstream's) are libstdc++ on Linux — they would reintroduce the very ABI coupling this design removed.

#### Decoupling: scan-deps is not required for a working CDB

clangd derives a translation unit's headers itself from the source + flags; `compile_commands.json` has one entry per source file, not per header. Scan-deps buys exactly two things: (a) the header→action index for *incremental* invalidation (section 4.4), and (b) optional header-entry emission so a header opens standalone. Neither is needed to emit a functional CDB.

Therefore Layer A can ship **without** in-process scan-deps — emit the CDB directly from aquery (the approach `kiron1/bazel-compile-commands` takes) — and add scan-deps purely for incrementality once the linkage spike succeeds. This decouples first user value from the project's biggest unknown and is the recommended sequencing.

Failure mode to handle: generated headers that have not been built. Scan-deps reports them as missing; we mirror the current tool's behavior of caching only when no headers are missing (see upstream [refresh.template.py](https://github.com/hedronvision/bazel-compile-commands-extractor/blob/main/refresh.template.py) for the rationale — rederive, do not copy). User-facing implication to state honestly: for codegen-heavy repos a build (or at least header generation) must precede a *complete* scan; "no full build required" is a clangd property, not a guarantee that every generated header resolves on first refresh.

### 4.3 De-Bazeling patches (the quirk inventory)

The rewrite must rederive these. The current Python script is a useful checklist; the patch logic itself is rederived from primary sources.

| Quirk                                                                                                      | Primary source for rederivation                                                               |
| ---------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| Apple `wrapped_clang`, `__BAZEL_XCODE_*` substitutions                                                     | Bazel `tools/osx/crosstool/wrapped_clang.cc`                                                  |
| Emscripten driver indirection                                                                              | emscripten `emcc` source, EM_COMPILER_WRAPPER hook                                            |
| NVCC to clang flag translation                                                                             | NVCC compiler driver docs, clang driver source                                                |
| MSVC `/showIncludes` locale strings                                                                        | Ninja issue 613, public MSVC documentation                                                    |
| ccache symlink resolution                                                                                  | ccache docs                                                                                   |
| Windows command-line length workaround                                                                     | MSDN command-line limits                                                                      |
| `//external` symlink, execroot trap                                                                        | Bazel `output_directories` docs                                                               |
| Execroot / absolute-path canonicalization (rewrite per-host `bazel-out`/cache paths to workspace-relative) | Bazel `output_directories` docs; required for the cross-host determinism property (section 9) |
| `parse_headers` action filtering                                                                           | Bazel cc rules source                                                                         |
| `compiler_param_file` feature disable                                                                      | Bazel cc features source                                                                      |
| `layering_check` feature disable                                                                           | Bazel cc features source                                                                      |
| `-fno-canonical-system-headers` strip                                                                      | Clang driver source, [clangd#1004](https://github.com/clangd/clangd/issues/1004)              |
| `-gcc-toolchain` strip                                                                                     | [clangd#1248](https://github.com/clangd/clangd/issues/1248)                                   |

This is roughly the full inventory. Each is a day or two of work, standalone-testable.

### 4.4 Sidecar storage

Two persistent files, in a single directory next to the CDB (default `.carve-cache/`):

```
entries-by-actionkey.binpb   # Binary proto: ActionRecord[]
headers-index.binpb          # Binary proto: HeaderIndex
```

Schema, defined in `carve.proto`. We use **Protobuf Edition 2024** (current as of project start in 2026, GA since protoc 27.0 with later releases adding 2024-specific features). Edition 2024 gives us:

- Default `string_type = VIEW`, so generated accessors return `absl::string_view`. Pairs naturally with C++23 + Abseil and removes per-call `std::string` allocations.
- Explicit field-presence opt-in via `features.field_presence` where we want it, default-implicit elsewhere.
- `import option` syntax if we ever need custom options without pulling in other symbols.
- Symbol visibility keywords (`export`, `local`) for clean module boundaries if the schema grows.

```proto
edition = "2024";
package carve;

message ActionRecord {
  string action_key = 1;
  repeated string sources = 2;
  repeated string headers = 3;
  repeated string command = 4;
  string project_id = 5 [features.field_presence = EXPLICIT];   // distinguish unset vs empty
  enum SourceKind {
    SOURCE_KIND_UNSPECIFIED = 0;
    PREPROCESSOR = 1;           // scan-deps result
    ASPECT_DECLARED = 2;        // future: from CcInfo
    ASPECT_M = 3;               // future: from aspect-scheduled -M
  }
  SourceKind source_kind = 6;
  int64 written_at = 7;         // unix seconds; for GC
}

message HeaderOwners {
  string header_path = 1;
  repeated string action_keys = 2;   // sorted; first is canonical owner
}

message HeaderIndex {
  repeated HeaderOwners owners = 1;
  uint32 schema_version = 2;
}
```

Binary proto for speed and schema discipline. `schema_version` lets future tool upgrades detect and rebuild stale sidecars. Editions also lets us upgrade to Edition 2026 (which enforces naming style by default) by changing one line; no schema migration if we name things conventionally from the start.

**`action_key` stability caveat.** The sidecar is keyed on aquery's `action_key`, which is stable run-to-run on a fixed Bazel version + configuration but *churns* across Bazel upgrades, toolchain changes, or `--config` changes. When the key space shifts, the diff in section 4.5 sees the entire own-project row set as `removed`+new and forces a full re-scan. `schema_version` covers *tool*-driven invalidation, not action-key churn. This is acceptable (correctness is preserved; only one slow refresh results) but must be documented so users are not surprised that switching `--config` triggers a full rebuild.

Fallback path: if a particular protoc release is fussy about Edition 2024 features, downgrade to `edition = "2023";` and lose only the `VIEW` default and a few cosmetic features. We do not need to fall back to `syntax = "proto3";`.

### 4.5 Merge semantics

Each `carve refresh` invocation owns exactly the rows whose `project_id` matches. On refresh:

1. Load current sidecar.
2. Run aquery, get action set `A_now`.
3. Partition own rows into `unchanged` (action_key in `A_now` with same content), `changed` (action_key in `A_now`, content differs), `removed` (action_key not in `A_now`).
4. For `changed` and new actions in `A_now`: run scan-deps, build new records.
5. For each affected header, update `HeaderOwners`. Canonical owner is lex-min of remaining action keys. This guarantees byte-stable header entries when the owner persists.
6. Other projects' rows are untouched.
7. Serialize sidecar atomically (write to `.tmp`, rename).
8. Emit CDB from full sidecar (all projects), atomically.

This makes the cross-project combined CDB a property of the data model, not a feature toggle.

### 4.6 Bazel rule (`cc_carve`)

Layer B. Defined in `cc_carve.bzl`:

```python
cc_carve(
    name = "compile_commands",
    targets = ["//..."],
    project_id = "main",                 # optional; defaults to workspace hash
    exclude_headers = "external",        # "all" | "external" | None
    exclude_external_sources = False,
)
```

Implementation runs `carve refresh` as a build action. Inputs: target labels (passed as args, not deps, because deps would force analysis of the whole world). Output: `compile_commands.json` plus the sidecar directory as declared outputs.

User runs `bazel build //:compile_commands`. Output appears under `bazel-bin/`, with a convenience symlink to the workspace root.

### 4.7 Aspect (Layer C)

Defined in `cc_carve_aspect.bzl`:

- `attr_aspects = ["deps", "data", "implementation_deps", "interface_deps", ...]` to propagate across the cc graph.
- For each visited target with `CcInfo`, register one action per compile action that runs `carve shard --action-key=... --command-file=... --source=...`.
- Output group `carve_shards` contains the per-action shard files.

Aggregator (`carve aggregate`) reads the shards as inputs to a separate top-level action, merges into CDB and sidecar. Bazel's action cache handles per-shard invalidation; aggregator only rebuilds when the set of shards or their contents changes.

Layer C is opt-in: `cc_carve(..., use_aspect = True)`. Layer A/B remain the default until C is proven on production repos.

### 4.8 Python footprint

Per the constraint, kept as close to zero as possible:

- `BUILD.bazel` and `.bzl` files: Starlark, not Python.
- Tests: GTest binaries in C++. No `py_test`.
- Vendored toolchain configuration: declarative `MODULE.bazel`, no scripts.
- Possible exception: a one-shot regeneration script for the NVCC flag table (a `nvcc_clang_diff.py`-style developer utility, to be added when the NVCC quirk lands). Could be rewritten in C++ but Python is genuinely faster to iterate on for that specific tool. Acceptable to keep as developer-only utility.

Net runtime Python: zero. Net developer Python: at most one script run rarely.

## 5. Build and dependency tree

`MODULE.bazel`:

```python
module(name = "carve", version = "0.1.0")

bazel_dep(name = "mbo", version = "...")                      # helly25/mbo. Pulls abseil and friends transitively
bazel_dep(name = "googletest", version = "1.15.2")
bazel_dep(name = "protobuf", version = "29.0")
bazel_dep(name = "rules_cc", version = "0.0.17")
bazel_dep(name = "rules_proto", version = "7.0.2")

# Hermetic clang toolchain AND linkable LLVM/Clang libraries from one ecosystem
# (hermeticbuild/hermetic-llvm). The @llvm-project overlay it exposes is built
# from source with the same libc++ as our code; //carve/scan_deps links
# `@llvm-project//clang:tooling`. See section 4.2 "Linkage reality".
bazel_dep(name = "llvm", version = "0.8.9")
register_toolchains("@llvm//toolchain:all", dev_dependency = True)
llvm_overlay = use_extension("@llvm//extensions:llvm.bzl", "llvm")
use_repo(llvm_overlay, "llvm-project")
```

mbo brings Abseil transitively, so we do not list Abseil separately. If a future mbo release stops re-exporting Abseil, add a direct `bazel_dep` then. Versions above are placeholders pinned at project bootstrap; lock to current-at-start releases.

C++ build:

- `cc_binary` target `//carve:carve` (with a `//:refresh` alias for `bazel run`).
- Per-module `cc_library` targets `//carve/<module>:<module>_cc`, using
  `implementation_deps` vs `deps` per the house convention (see [RULES.md](RULES.md)).
- Linked statically where feasible. LLVM libs typically static.
- Compiled with `-std=c++23`, `-stdlib=libc++` (under the hermetic-llvm clang toolchain), `-Wall -Wextra -Werror`, `-Wpedantic`. LLVM's own sources build at C++17 (scoped in `.bazelrc`).
- Sanitizer presets: `--config=asan`, `--config=tsan`, `--config=ubsan` for tests.

Test layout:

- Per-module GTest `//carve/<module>:<module>_test`, `size = "small"`, colocated
  with the unit as `<module>_test.cc`.
- `//carve/e2e:end_to_end_test` integration tests that drive `carve` against synthetic Bazel workspaces in `testdata/`.
- Quirk-specific golden tests: one input action, one expected output entry, per platform patch.

## 6. CLI surface

Driven by `absl::Flags`. Subcommand pattern:

```
carve refresh [flags] [-- bazel-flags]
  --output=PATH                CDB path. Default: compile_commands.json at workspace root
  --project-id=ID              Override default (workspace path hash)
  --targets=PATTERN            Target pattern, repeatable. Default: //...
  --exclude-headers=MODE       all | external | none
  --exclude-external-sources   Bool
  --jobs=N                     Scan-deps parallelism. Default: hardware concurrency
  --bazel=PATH                 Path to bazel binary. Default: $PATH lookup
  -- ...                       Bazel flags forwarded to aquery

carve aggregate [flags]
  --shards=DIR                 Shard input directory (Layer C)
  --output=PATH
  --project-id=ID

carve shard [flags]
  --action-key=KEY
  --command-file=PATH          Argv as protobuf
  --source=PATH
  --out=PATH                   Shard output (ActionRecord protobuf)

carve prune [flags]
  --age=DAYS                   Drop rows older than this with no recent refresh
  --project-id=ID              Restrict to a project
```

All flags `absl::Flags`. Help auto-generated. No hand-rolled arg parsing.

## 7. Distribution and bootstrap

Two delivery modes:

1. **As a bzlmod dependency.** Consumers add `bazel_dep(name = "carve")`. The `cc_carve` macro is loaded from `@carve//rules:cc_carve.bzl`. First use builds `carve` from source through their toolchain. Subsequent uses hit Bazel's cache.
2. **As prebuilt binaries.** Released for common platforms (darwin-arm64, darwin-x86_64, linux-x86_64, linux-arm64, windows-x86_64) via GitHub Releases. `cc_carve` rule downloads the appropriate binary for the host. Avoids the from-source build entirely for users on supported platforms.

Mode 2 matters for the editor-tooling use case: contributors want compile_commands.json working immediately after clone, not after a 5-minute LLVM toolchain build.

## 8. Coexistence with the current tool

Until the rewrite reaches parity:

- Live in a different repo / module name (`carve` vs `hedron_compile_commands`).
- Different output path default if needed, though `compile_commands.json` is the only sensible default.
- Document a migration path in README: drop one macro call, add the other.
- Validation harness: run both tools against a corpus of projects (this repo's `print_args.cpp`, public Bazel-using OSS repos), diff the CDBs, investigate every meaningful difference. Goal: at parity, identical entries up to ordering, modulo documented intentional differences.

Do not deprecate or break the existing path until validation passes on the corpus.

## 9. Testing strategy

Tests are written at every ring below, not just the unit level. One-shot manual
verification (running `carve` by hand, a throwaway diff) is allowed only as
*planning input*: it informs which committed test to write, and the change is
not done until that test exists. See the testing-discipline section in
[AGENTS.md](AGENTS.md).

Concentric rings:

1. **Unit tests.** Per module. Heavy on the command-patching logic since that is where regressions hide.
2. **Golden tests.** Action input plus expected entry output. One golden per quirk in the inventory. Stored as protobuf text-format for diffability.
3. **Integration tests.** Synthetic Bazel workspaces under `testdata/`. Drive `carve refresh`, snapshot the resulting CDB, diff against golden CDB. Cover:
   - Plain C++ library + binary
   - Generated headers (via `genrule`)
   - External dependency (via `bazel_dep`)
   - Apple-platform cross-compile (skipped on non-Apple)
   - Windows MSVC (skipped on non-Windows)
   - NVCC (skipped if no CUDA toolkit)
4. **Differential tests.** Run rewrite and existing tool against the same workspace; diff output. Acceptable as a one-off validation harness, not in CI.
5. **Property tests.** Idempotency (refresh twice in a row produces identical sidecar and CDB), determinism (refresh under stable input produces identical output across runs and across hosts of the same platform). **Note:** cross-host determinism holds *only after* execroot/absolute-path canonicalization (section 4.3) — raw aquery command lines embed per-host cache paths (`/home/<user>/.cache/bazel/...`) and are not byte-identical across machines. The property test must run on canonicalized output, and the canonicalization patch is a prerequisite, not an optional quirk.

### 9.1 Assertion strategy: match the model, not the serialization

Assert on the structured data, never on a serialized blob:

- **Proto data (sidecar records; any future proto model).** Use the helly25/proto
  matchers (`@com_helly25_proto//mbo/proto:matchers_cc`, namespace `mbo::proto`) —
  `EqualsProto`, `Partially(EqualsProto(...))`, `WhenDeserialized`,
  `IgnoringRepeatedFieldOrdering` — never `Eq(msg.SerializeAsString())`. They give
  field-level diffs on failure and accept text-proto literals for expectations.
  Plain C++ structs (e.g. `cdb::CompileCommand`) use stock gmock `ElementsAre`/
  `Field` for the same reason — assert fields, not strings.
- **`compile_commands.json` is output, not a test surface.** It is the *only*
  interface clangd consumes (no proto channel), and `cdb::ToJson` is
  byte-deterministic by design. So the serializer's exact bytes are pinned with
  exact-string golden tests (ring 2), and byte-stable e2e/idempotency (ring 5)
  diff the whole file — neither needs a JSON matcher.
- **Semantic JSON comparison is a differential-only need (ring 4).** Diffing
  carve's CDB against a *different* producer (Hedron) means key/array order and
  whitespace differ. Get that comparison by parsing the JSON back into the proto
  model and reusing the proto matchers (`IgnoringRepeatedFieldOrdering` /
  `Partially`), or with off-the-shelf `jq -S` / `jd` in the one-off harness —
  never a carve-local JSON comparator. A JSON-native matcher, if ever needed,
  belongs in mbo (its `mbo/json` would need a parser first; today it only
  builds/serializes).

## 10. Open questions

- **Aquery proto vendoring (decided, not open).** `analysis_v2.proto` is not published as a bzlmod module; it lives in `bazelbuild/bazel/src/main/protobuf` and its `import "build.proto"` pulls in a further chain (`stardoc_output.proto`, ...). carve consumes only the aquery action graph, never cquery, so we vendor a **trimmed, self-contained** copy at `carve/third_party/bazel/analysis_v2.proto`: the `ConfiguredTarget`/`CqueryResult` messages and the `build.proto` import they need are removed, every other message is byte-for-byte upstream. This avoids vendoring the whole proto chain. **Version-couple the vendored copy to our Bazel pin** (note the v1→v2 id type change, `string`→`uint64`, gated by `--incompatible_proto_output_v2`) and re-vendor on every Bazel bump — a recurring maintenance task, not a one-time setup.
- **DependencyScanningService linkage (resolved).** See section 4.2 "Linkage reality": carve links `@llvm-project//clang:tooling` (which pulls `:dependency_scanning`) built from source via the hermetic-llvm `llvm` module, with the same libc++ as our code. `DependencyScanningTool` links cleanly — validated on macOS and Linux in CI. Cost is a ~12-min cold compile of the LLVM/Clang subset, cacheable.
- **Modules support timing.** Phase the modules-aware path in later. Scan-deps already handles modules; we just need a flag to expose it. Defer until a real consumer asks.
- **Remote execution.** Layer C with remote cache is the long-tail goal. Layer A on a developer laptop is the immediate goal. Make sure Layer A does not preclude Layer C in the schema.
- **Symlink handling.** The current tool's `//external` link choreography (see upstream [refresh.template.py](https://github.com/hedronvision/bazel-compile-commands-extractor/blob/main/refresh.template.py), the external-symlink section) is subtle. Rederive carefully on Windows where junctions differ from symlinks. Candidate for an mbo utility if not already present.
- **mbo surface gaps.** Inventory what we need against what mbo ships at project start. Anything missing becomes an mbo PR before the corresponding `carve` code lands. Avoid the failure mode of "we wrote local helpers because mbo did not have it yet" calcifying into duplicate utility code.
- **Multi-version Bazel support.** Aim at Bazel 9.1 minimum; check whether earlier versions are worth supporting. Probably not in the first release.

## 11. Phasing

> Current status and the up-to-date, dependency-ordered task breakdown live in
> [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md). The month-by-month
> sketch below is the original design-time roadmap, kept for context.

Concrete sequence for the first six months:

| Month | Milestone                                                                                                                                                                                                                                                                                         |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1     | Repo bootstrap, MODULE.bazel, toolchain pin, hello-world `carve` binary, GTest wired up. **LLVM-libs linkage spike** (section 4.2): prove `DependencyScanningTool` links into a `cc_binary`. *Resolved — source-built via hermetic-llvm (`@llvm-project//clang:tooling`).* This gates months 3–4. |
| 2     | `aquery` module + vendored proto parsing; basic `command` module with first three quirks (incl. execroot canonicalization); CDB writer. **Layer A emits a working CDB straight from aquery, no scan-deps yet.** Stand up a crude differential harness against Hedron on this repo.                |
| 3     | Sidecar persistence, action-keyed diff, scan-deps integration (single-threaded) — assuming the month-1 spike succeeded; otherwise carry the no-scan-deps Layer A and reschedule.                                                                                                                  |
| 4     | Full quirk inventory ported, scan-deps parallelized, merge mode, Layer A feature-complete                                                                                                                                                                                                         |
| 5     | `cc_carve` rule (Layer B), integration test corpus, differential test harness hardened                                                                                                                                                                                                            |
| 6     | Public 0.1 release; begin Layer C prototype                                                                                                                                                                                                                                                       |

This is aggressive but plausible if the implementer is focused. Real schedules slip; the layering ensures each month produces a shippable improvement. The deliberate move here is putting the riskiest dependency (LLVM linkage) and the most valuable validation artifact (differential harness) at the *front*, so a bad surprise lands in month 1–2 rather than month 3–5.

## 12. Out of scope

To prevent scope creep:

- Generating `.clangd` config alongside the CDB. Users own their `.clangd`.
- Integration with non-clang tooling that wants a CDB (rtags, cquery). Standard JSON output; downstream tools work or they do not.
- Cross-platform binary caching across projects. That is Bazel's remote cache concern.
- A GUI. No.
- Supporting build systems other than Bazel. Different project.

## 13. License

Apache-2.0. Selected for: permissive use, explicit patent grant, broad enterprise acceptance, compatibility with most consumers including the existing Hedron tool's downstream consumers who might want to switch.

Decision before code is written. Every file gets the SPDX identifier from commit zero.

## 14. Related projects

The landscape carve enters. Useful for cross-referencing approaches, learning from past designs, and understanding where carve fits.

A few entries worth singling out before the full list:

- **[kiron1/bazel-compile-commands](https://github.com/kiron1/bazel-compile-commands)** is the closest architectural precedent to carve: a Rust CLI parsing `bazel aquery` output. Same idea, different language, no in-process scan-deps. Worth reading before we start coding.
- **[Arech/yacce](https://github.com/Arech/yacce)** is the strace-based alternative path. Comparison point for "what if we did not use aquery at all."
- **[mikael-s-persson/bazel_cc_meta](https://github.com/mikael-s-persson/bazel_cc_meta)** is aspect-based and adds header/dep analysis on top. Closest to the Layer C aspect path we describe in section 4.7.
- The fork ecosystem around Hedron (helly25, modular, chandlerc, swift-nav, zaucy, tinted-software) confirms that upstream cadence is the dominant pain point. Carve does not try to be a fork; it is a clean replacement.

### 14.1 Bazel-specific compile_commands.json generators

- **bazel-compile-commands-extractor** ([hedronvision/bazel-compile-commands-extractor](https://github.com/hedronvision/bazel-compile-commands-extractor)) - MIT-like (NOASSERTION on GitHub; see LICENSE). Aquery-based generator; de-facto standard for Bazel + clangd, no full build required. Status: active.
- **bazel-compilation-database** ([grailbio/bazel-compilation-database](https://github.com/grailbio/bazel-compilation-database)) - Apache-2.0. Aspect-based generator; original popular implementation, now in maintenance mode. Status: archived.
- **bazel-compile-commands** ([kiron1/bazel-compile-commands](https://github.com/kiron1/bazel-compile-commands)) - unknown (NOASSERTION). Rust CLI that parses `bazel aquery` output to emit compile_commands.json without modifying the workspace. Status: active.
- **bazel_cc_meta** ([mikael-s-persson/bazel_cc_meta](https://github.com/mikael-s-persson/bazel_cc_meta)) - Apache-2.0. Aspect-based tool that also analyzes header exports and missing/unused dependencies. Status: active.
- **bazel-clangd-helper** ([figurerobotics/bazel-clangd-helper](https://github.com/figurerobotics/bazel-clangd-helper)) - Apache-2.0. Aspect-based generator focused on a Bzlmod-friendly setup. Status: active.
- **yacce** ([Arech/yacce](https://github.com/Arech/yacce)) - MIT. strace-based interception wrapper that extracts compile_commands.json from any local Bazel (or other) build precisely. Status: active.
- **compdb-bazel** ([thoren-d/compdb-bazel](https://github.com/thoren-d/compdb-bazel)) - unknown. Small aquery-based Python script focused on Windows / clang-cl Bazel C++ targets. Status: dormant.
- **Bazel_and_CompileCommands** ([vincent-picaud/Bazel_and_CompileCommands](https://github.com/vincent-picaud/Bazel_and_CompileCommands)) - unknown. Setup script using Bazel's experimental action_listener plus a Python script to produce compile_commands.json. Status: dormant.
- **bazel-compilation-db** ([tolikzinovyev/bazel-compilation-db](https://github.com/tolikzinovyev/bazel-compilation-db)) - unknown. Adaptation of Kythe's extractor approach. Status: archived.
- **bazel-compdb** ([ramblehead/bazel-compdb](https://github.com/ramblehead/bazel-compdb)) - unknown. Small set of Bazel rules for generating compile_commands.json. Status: dormant.
- **bazel-compilation-database** ([xulongwu4/bazel-compilation-database](https://github.com/xulongwu4/bazel-compilation-database)) - Apache-2.0. Independent maintained re-implementation of the grailbio aspect-based generator. Status: active.
- **bazel_compilation_database** ([cherrry/bazel_compilation_database](https://github.com/cherrry/bazel_compilation_database)) - Apache-2.0. Fork of the grailbio generator with local customizations. Status: dormant.
- **bazel_compile_commands** ([cdump/bazel_compile_commands](https://github.com/cdump/bazel_compile_commands)) - Apache-2.0. extra_action / action_listener-based generator (legacy approach). Status: dormant.
- **bazel_compilation_database** ([curiousleo/bazel_compilation_database](https://github.com/curiousleo/bazel_compilation_database)) - unknown. Early generator for YouCompleteMe semantic completion against Bazel C++. Status: dormant.

### 14.2 Notable forks of hedronvision/bazel-compile-commands-extractor

- **helly25/bazel-compile-commands-extractor** ([helly25/bazel-compile-commands-extractor](https://github.com/helly25/bazel-compile-commands-extractor)) - same license as upstream (NOASSERTION). Restricted-maintenance fork (this repo) carrying focused fixes and design docs while upstream is slow-moving. Status: active.
- **modular/bazel-compile-commands-extractor** ([modular/bazel-compile-commands-extractor](https://github.com/modular/bazel-compile-commands-extractor)) - inherits upstream license. Modular's downstream fork tracking changes needed for their toolchain. Status: active.
- **chandlerc/bazel-compile-commands** ([chandlerc/bazel-compile-commands](https://github.com/chandlerc/bazel-compile-commands)) - inherits upstream license. Chandler Carruth's personal fork (Carbon/LLVM-adjacent experiments). Status: active.
- **swift-nav/bazel-compile-commands-extractor** ([swift-nav/bazel-compile-commands-extractor](https://github.com/swift-nav/bazel-compile-commands-extractor)) - inherits upstream license. Swift Navigation's downstream fork. Status: active.
- **zaucy/bazel-compile-commands-extractor** ([zaucy/bazel-compile-commands-extractor](https://github.com/zaucy/bazel-compile-commands-extractor)) - inherits upstream license. Independent contributor's fork with patches not yet merged upstream. Status: active.
- **tinted-software/bazel-compile-commands-extractor** ([tinted-software/bazel-compile-commands-extractor](https://github.com/tinted-software/bazel-compile-commands-extractor)) - inherits upstream license. Org-level fork with internal patches. Status: active.

### 14.3 Compile commands generators for other build systems

- **CMake (CMAKE_EXPORT_COMPILE_COMMANDS)** ([Kitware/CMake](https://github.com/Kitware/CMake)) - BSD-3-Clause. Built-in compile_commands.json emission for the Makefile and Ninja generators. Status: active.
- **Ninja (`ninja -t compdb`)** ([ninja-build/ninja](https://github.com/ninja-build/ninja)) - Apache-2.0. Built-in compdb tool that converts the Ninja build graph into compile_commands.json. Status: active.
- **Bear** ([rizsotto/Bear](https://github.com/rizsotto/Bear)) - GPL-3.0. LD_PRELOAD / wrapper-based interceptor that produces compile_commands.json from arbitrary builds. Status: active.
- **Meson** ([mesonbuild/meson](https://github.com/mesonbuild/meson)) - Apache-2.0. Build system that emits compile_commands.json automatically into the build directory. Status: active.
- **compiledb** ([nickdiego/compiledb](https://github.com/nickdiego/compiledb)) - GPL-3.0. Python tool that parses `make -n` dry-run output into compile_commands.json without LD_PRELOAD. Status: dormant.
- **scan-build / intercept-build** ([rizsotto/scan-build](https://github.com/rizsotto/scan-build)) - LLVM/MIT-like. Sibling of Bear bundled with Clang Static Analyzer's scan-build. Status: active.

### 14.4 Clang/LLVM tooling that consumes compile_commands.json

- **clangd** ([llvm/llvm-project](https://github.com/llvm/llvm-project)) - Apache-2.0 with LLVM Exception. C/C++ language server; primary consumer of compile_commands.json. Status: active.
- **clang-tidy** ([llvm/llvm-project](https://github.com/llvm/llvm-project)) - Apache-2.0 with LLVM Exception. LibTooling-based linter / static analyzer driven by compile_commands.json. Status: active.
- **clang-scan-deps** ([llvm/llvm-project](https://github.com/llvm/llvm-project)) - Apache-2.0 with LLVM Exception. Dependency scanner (header + C++20 module deps) that reads compile_commands.json. Status: active.
- **clang-format** ([llvm/llvm-project](https://github.com/llvm/llvm-project)) - Apache-2.0 with LLVM Exception. Formatter; can use a compilation database in some integrations. Status: active.
- **ccls** ([MaskRay/ccls](https://github.com/MaskRay/ccls)) - Apache-2.0. Alternative C/C++ language server consuming compile_commands.json. Status: active.
- **CodeChecker** ([Ericsson/codechecker](https://github.com/Ericsson/codechecker)) - Apache-2.0. Static analysis driver (Clang SA, clang-tidy, Cppcheck) that takes compile_commands.json as input. Status: active.
- **include-what-you-use** ([include-what-you-use/include-what-you-use](https://github.com/include-what-you-use/include-what-you-use)) - NCSA. Clang-based include analyzer; `iwyu_tool.py` consumes compile_commands.json. Status: active.
- **rtags** ([Andersbakken/rtags](https://github.com/Andersbakken/rtags)) - GPL-3.0. Clang-based C/C++ tagger/indexer that ingests a compilation database. Status: active.
- **Sourcetrail** ([CoatiSoftware/Sourcetrail](https://github.com/CoatiSoftware/Sourcetrail)) - GPL-3.0. Code visualization/exploration tool that imported compile_commands.json. Status: archived (project closed 2021).

### 14.5 Bazel-clangd integration approaches NOT using compile_commands.json

- **clangd `.clangd` / `compile_flags.txt` config** ([clangd docs](https://clangd.llvm.org/config)) - Apache-2.0. clangd's native YAML/flags config; can substitute for a CDB in simple projects but lacks the file list needed for background indexing. Status: active.
- **IntelliJ / CLion Bazel plugin** ([bazelbuild/intellij](https://github.com/bazelbuild/intellij)) - Apache-2.0. JetBrains-side path that builds its own C++ workspace model from Bazel aspects rather than going through compile_commands.json. Status: active.
- **Tulsi** ([bazelbuild/tulsi](https://github.com/bazelbuild/tulsi)) - Apache-2.0. Original Xcode project generator for Bazel; superseded by rules_xcodeproj. Status: archived.
- **rules_xcodeproj** ([MobileNativeFoundation/rules_xcodeproj](https://github.com/MobileNativeFoundation/rules_xcodeproj)) - MIT. Generates Xcode projects from Bazel targets, supplying Xcode's own indexing pipeline instead of a CDB. Status: active.
- **vscode-bazel** ([bazel-contrib/vscode-bazel](https://github.com/bazel-contrib/vscode-bazel)) - Apache-2.0. VS Code Bazel extension providing Starlark LSP, buildifier, target navigation; does not itself produce a CDB for C++. Status: active.
- **bazel-eclipse / bazel-vscode-java** ([salesforce/bazel-eclipse](https://github.com/salesforce/bazel-eclipse)) - Apache-2.0. Eclipse JDT-LS-based Bazel integration for Java; non-CDB IDE path. Status: active.

### 14.6 Adjacent indexing / code-search systems

- **Kythe** ([kythe/kythe](https://github.com/kythe/kythe)) - Apache-2.0. Google's language-agnostic code-indexing ecosystem with its own Bazel extractor. Status: active.
- **Glean** ([facebookincubator/Glean](https://github.com/facebookincubator/Glean)) - MIT-like. Meta's facts-about-code indexing system, consuming SCIP/LSIF and language-specific indexers. Status: active.
- **SCIP** ([sourcegraph/scip](https://github.com/sourcegraph/scip)) - Apache-2.0. Sourcegraph's successor to LSIF; modern code-graph indexing format. Status: active.
- **Sourcegraph** ([sourcegraph/sourcegraph-public-snapshot](https://github.com/sourcegraph/sourcegraph-public-snapshot)) - NOASSERTION (mixed). Code search/intelligence platform that ingests SCIP/LSIF indexes. Status: archived (public snapshot).
- **SciTools Understand** (proprietary; [scitools.com](https://scitools.com/)) - commercial. Long-running closed-source code comprehension / static analysis IDE. Status: active.

### 14.7 Higher-level editor integrations that depend on a CDB

- **vscode-clangd** ([clangd/vscode-clangd](https://github.com/clangd/vscode-clangd)) - MIT. Official VS Code extension wrapping the clangd language server; the most common consumer of Bazel-produced compile_commands.json. Status: active.
- **vscode-cpptools** ([microsoft/vscode-cpptools](https://github.com/microsoft/vscode-cpptools)) - Microsoft proprietary. VS Code's Microsoft C/C++ extension; can use compile_commands.json as a configuration provider. Status: active.
- **nvim-lspconfig** ([neovim/nvim-lspconfig](https://github.com/neovim/nvim-lspconfig)) - Apache-2.0. Neovim's canonical LSP configurations including clangd/ccls. Status: active.
- **lsp-mode** ([emacs-lsp/lsp-mode](https://github.com/emacs-lsp/lsp-mode)) - GPL-3.0. Emacs LSP client that drives clangd/ccls against compile_commands.json. Status: active.
- **YouCompleteMe** ([ycm-core/YouCompleteMe](https://github.com/ycm-core/YouCompleteMe)) - GPL-3.0. Vim semantic completion engine; uses compile_commands.json for C-family languages. Status: active.
- **CodeChecker VS Code plugin** ([Ericsson/CodecheckerVSCodePlugin](https://github.com/Ericsson/CodecheckerVSCodePlugin)) - Apache-2.0. VS Code UI for CodeChecker static analysis runs driven by compile_commands.json. Status: active.
- **vscode-iwyu** ([helly25/vscode-iwyu](https://github.com/helly25/vscode-iwyu)) - Apache-2.0. VS Code extension running include-what-you-use over compile_commands.json. Sibling project under helly25. Status: active.
