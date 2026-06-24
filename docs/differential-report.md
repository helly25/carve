<!--
SPDX-FileCopyrightText: Copyright (c) The helly25/carve authors (github.com/helly25/carve)
SPDX-License-Identifier: Apache-2.0
-->

# Differential validation (M3)

This is the M3 correctness gate (CARVE_DESIGN.md §8, §9.4): validate that the
compilation database carve produces is consumable by clangd and that carve's
output differs from the reference extractor (Hedron) only in ways we can
explain. It pairs with the reusable harness `tools/cdb_diff.py`.

Last run: 2026-06-24, `//carve/...` on this repo (22 compile actions),
macOS arm64, hermetic `llvm` toolchain (clang 22.1.7).

## 1. clangd consumes carve's CDB

`carve refresh --targets=//carve/...` was run, then `clangd --check` was pointed
at the produced `compile_commands.json` for `carve/cdb/cdb.cc`:

```
clangd --check=<execroot>/carve/cdb/cdb.cc --compile-commands-dir=<out>
=> Loaded compilation database ... Building preamble ... Building AST ...
   Indexing AST ... All checks completed, 8 errors
```

- The preamble, AST, and index all build with **zero compilation diagnostics**.
- The "8 errors" are clangd `--check`'s per-token *tweak* probes
  (`ExtractFunction ==> FAIL: Cannot extract break/continue ...`), not problems
  with the source or the command — they fire at every `break`/`continue` token
  regardless of the CDB.

**Finding — use a toolchain-matched clangd.** clangd injects a `-resource-dir`
and resolves the sysroot from the compiler named in the command. With a clangd
that matches the build toolchain (clang 22.1.7, the one the hermetic `llvm`
module ships), it uses the hermetic clang builtins + the from-source libc++ named
in the command and parses cleanly. A *mismatched* system clangd (e.g. Apple
clang 21 + the macOS SDK libc++) substitutes its own resource-dir and SDK, which
can surface spurious diagnostics. Editors should point at the toolchain's clangd
(carve's `.clangd` / IDE config), not a system one.

## 2. Generated headers and the missing-header guard (observed)

On a clean tree, `carve refresh //carve/...` reported:

```
carve: wrote 22 entries (22 scanned, 0 reused)
carve: warning: 10 action(s) have unresolved headers (unbuilt generated headers?); re-scanned next refresh
```

The 10 unresolved actions are the ones whose sources `#include` Bazel-generated
proto headers (`carve/sidecar/carve.pb.h`, the vendored `analysis_v2.pb.h`) that
do not exist on disk until built. This is exactly the CARVE_DESIGN §4.2 failure
mode: the CDB entries are still emitted (clangd works), but those records are
left unstamped so a later refresh re-scans them once the headers are generated.
Building first (`bazel build //carve/...`) and re-refreshing resolves them. This
is a property of any scan-deps extractor, not a carve regression.

## 3. carve vs Hedron — explained differences

carve is a clean-slate replacement for Hedron's
`bazel-compile-commands-extractor`; it deliberately diverges in several places.
The harness (`tools/cdb_diff.py`) keys entries by workspace-relative source and
compares normalized flag sets (ignoring volatile output/dep/seed tokens); the
*structural* differences below are by design, not diffs the harness should flag:

| Aspect             | carve                                                                                            | Hedron                                                       |
| ------------------ | ------------------------------------------------------------------------------------------------ | ------------------------------------------------------------ |
| Header discovery   | in-process clang `DependencyScanningTool` (scan-deps)                                            | forks `clang -M` / parses `/showIncludes` per source         |
| Incrementality     | binary-proto sidecar keyed by aquery `action_key`, command+mtime staleness, project-scoped merge | re-extracts; caches action keys in `bazel-out`               |
| `file`/`directory` | absolute against the execroot; argv stays exec-relative                                          | workspace-relative paths via `bazel-out`/`external` symlinks |
| Header index       | persisted header -> owning-action index for targeted invalidation                                | none                                                         |

The clangd-facing argv transforms are the **same intent** and should diff empty
on shared flags once both are de-Bazeled: strip `-fno-canonical-system-headers`
(clangd#1004) and `-gcc-toolchain` (clangd#1248), drop a leading `ccache`
wrapper, drop MSVC `/showIncludes`, and resolve Apple `wrapped_clang`
`__BAZEL_XCODE_*` placeholders (carve `command` module; CARVE_DESIGN §4.3).

## 4. Running a live diff

`tools/cdb_diff.py` compares any two compilation databases:

```
tools/cdb_diff.py <carve>/compile_commands.json <reference>/compile_commands.json
```

It reports sources present in only one CDB and, for shared sources, the
normalized flag differences (volatile `-o` / `-MF` / `-frandom-seed=` / compile-dir
tokens are ignored so a real flag delta stands out). A built-in `--selftest`
covers the keying and normalization and runs in CI.

Producing the reference (Hedron) CDB requires wiring Hedron into a workspace and
running `@hedron_compile_commands//:refresh_all`; that is intentionally **not**
added to carve (the tool it replaces). The harness is ready to consume a Hedron
CDB generated in any consumer workspace.
