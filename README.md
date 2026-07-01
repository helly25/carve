# carve

Carve `compile_commands.json` out of Bazel build graphs for clangd-style
tooling. A clean-slate C++23 replacement for the
[Hedron bazel-compile-commands-extractor](https://github.com/hedronvision/bazel-compile-commands-extractor),
under [Apache-2.0](LICENSE).

## Status

Working, pre-release. All three layers are implemented and tested - Layer A
(`carve refresh`), Layer B (`bazel run //:refresh`), and Layer C (the
`cc_carve_aspect` aspect emitting per-action shards) - along with the `refresh`,
`aggregate`, `shard`, and `prune` subcommands. Not yet published to the Bazel
Central Registry; the release tooling is in place. See
[CARVE_DESIGN.md](CARVE_DESIGN.md) for the architecture and
[docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md) for milestone status.

## What it does

1. Walks Bazel's action graph via `bazel aquery`.
2. Filters to C/C++/Objective-C/CUDA compile actions.
3. Resolves each action's header set in-process via clang's
   `DependencyScanningTool`.
4. De-Bazels each compile command so clangd can introspect it without
   Bazel-specific environment.
5. Writes an atomic `compile_commands.json` plus a persistent sidecar cache
   that skips re-scanning unchanged actions on re-refresh.

A working CDB does not require a full build; clangd resolves most headers
itself. Generated headers, however, must exist on disk for the scan to resolve
them - codegen-heavy targets may need a build first. Sub-second incremental
refresh on large monorepos is a Layer C (aspect) property: Layers A/B re-run
`bazel aquery` each time and so pay the graph-query cost regardless of edit
size. See [CARVE_DESIGN.md](CARVE_DESIGN.md) section 3.1.

## Usage

```bash
# Refresh the whole repo's compilation database (writes compile_commands.json to
# the workspace root). This is the carve_refresh rule (Layer B).
bazel run //:refresh

# Or drive the binary directly (Layer A), choosing targets/output on the CLI.
bazel run //:carve -- refresh --targets=//foo/... --output=compile_commands.json
```

In a consumer workspace, depend on carve in `MODULE.bazel` (once it is published
to the Bazel Central Registry):

```python
bazel_dep(name = "helly25_carve", version = "<release>")
```

then add the rule from a `BUILD` file:

```python
load("@helly25_carve//rules:carve.bzl", "carve_refresh")

carve_refresh(name = "refresh", targets = ["//..."])
```

`carve_refresh` is a **`bazel run`** target, not a build artifact: carve invokes
`bazel aquery`, and spawning bazel inside a build action is the nested-bazel
trap. For huge repos there is also Layer C - `cc_carve_aspect` +
`carve_aspect_refresh` (`rules/cc_carve_aspect.bzl`, `rules/carve.bzl`) schedule
one individually-cacheable shard per compile action and aggregate them.

## Build requirements

- Bazel 9.1+
- Clang 22.x (LLVM 22.1.7), pinned via the hermetic
  [llvm](https://github.com/hermeticbuild/hermetic-llvm) toolchain - which also
  supplies the LLVM libraries `scan_deps` links, built from source
- Apple Silicon / x86\_64 Linux supported; Windows planned

## License

Apache-2.0. See [LICENSE](LICENSE).
