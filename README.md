# carve

Carve `compile_commands.json` out of Bazel build graphs for clangd-style
tooling. A clean-slate C++23 replacement for the
[Hedron bazel-compile-commands-extractor](https://github.com/hedronvision/bazel-compile-commands-extractor),
under [Apache-2.0](LICENSE).

## Status

Pre-alpha. The repository currently contains only the design and bootstrap.
See [CARVE_DESIGN.md](CARVE_DESIGN.md) for the architecture, layering, and
six-month build-out plan.

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
them — codegen-heavy targets may need a build first. Sub-second incremental
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

In a consumer workspace, add the rule from a `BUILD` file:

```python
load("@helly25_carve//rules:carve.bzl", "carve_refresh")

carve_refresh(name = "refresh", targets = ["//..."])
```

`carve_refresh` is a **`bazel run`** target, not a build artifact: carve invokes
`bazel aquery`, and spawning bazel inside a build action is the nested-bazel
trap. Layer C (an aspect emitting per-action shards, opt-in) is not yet
implemented.

## Build requirements

- Bazel 9.1+
- Clang 22.x (LLVM 22.1.7), pinned via the hermetic
  [llvm](https://github.com/hermeticbuild/hermetic-llvm) toolchain — which also
  supplies the LLVM libraries `scan_deps` links, built from source
- Apple Silicon / x86\_64 Linux supported; Windows planned

## License

Apache-2.0. See [LICENSE](LICENSE).
