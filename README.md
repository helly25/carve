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

## Usage (planned)

```bash
# Layer A: single-shot tool.
bazel run @helly25_carve//:refresh

# Layer B: CDB as a Bazel build artifact.
bazel build //:compile_commands

# Layer C: aspect-driven, per-action shards (opt-in).
bazel build //:compile_commands --@helly25_carve//rules:use_aspect=True
```

## Build requirements

- Bazel 9.1+
- Clang 22.x (LLVM 22.1.7), pinned via the hermetic
  [llvm](https://github.com/hermeticbuild/hermetic-llvm) toolchain — which also
  supplies the LLVM libraries `scan_deps` links, built from source
- Apple Silicon / x86\_64 Linux supported; Windows planned

## License

Apache-2.0. See [LICENSE](LICENSE).
