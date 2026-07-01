# RULES.md

Code-style and structural rules for `helly25/carve`. These mirror the helly25
house style (see sibling repos [mbo](https://github.com/helly25/mbo) and
[bzl](https://github.com/helly25/bzl)).

[STYLE_CPP.md](STYLE_CPP.md) is the detailed C++ style companion - the shared
helly25 C++ conventions with rationale and examples (idioms, error handling,
output/`AbslStringify`, concurrency, protobuf, GoogleTest). This file is the
concise carve-specific rule set and structural conventions; STYLE_CPP.md expands
the C++ parts. Where they overlap they agree; STYLE_CPP.md has the detail.

[AGENTS.md](AGENTS.md) remains canonical for *process and agent* conventions
(commits, PRs, testing discipline, library priority) and references this file
for *code style*. [CARVE_DESIGN.md](CARVE_DESIGN.md) is canonical for
architecture. Where this file and the design disagree, the design wins and this
file is corrected.

## Licensing and headers

- Apache-2.0. Every Bazel, Starlark, C++, proto, and shell file starts with the
  SPDX header block (see [AGENTS.md](AGENTS.md#file-headers)). Markdown, JSON,
  and other comment-less formats are exempt.
- Unix text files: UTF-8, LF line endings, final newline, no trailing
  whitespace.

## C++

- C++23, clang 20.1+ (pinned at LLVM 22.1.7 via the hermetic-llvm `llvm` module).
- Style: Google C++ with the deviations in [.clang-format](.clang-format);
  formatting is enforced, not negotiated.
- Lints: [.clang-tidy](.clang-tidy) with `WarningsAsErrors: true`.
- No exceptions (`-fno-exceptions`); errors flow through `absl::Status`,
  `absl::StatusOr`, or `std::expected`.

### Layout and naming

- **One module per directory:** `carve/<module>/`, self-contained.
- **Namespaces:** `carve::<module>`. Internal-only code lives in
  `carve::<module>::<module>_internal` - never a bare `internal` namespace.
  Implementation detail may use a nested `detail` namespace.
- **Header guards:** `CARVE_<PATH>_<FILE>_` (path + filename, upper-snake,
  non-alphanumerics to `_`, trailing `_`). Example:
  `CARVE_COMMAND_DEBAZEL_H_`.
- **Macros:** prefix `CARVE_`. **Flags:** `--carve_<module>_*`.
- **Includes:** project headers (quoted, full repo-relative path) before
  external; keep IWYU pragmas in headers (`// IWYU pragma: keep`).
- **Files:** `<unit>.h` / `<unit>.cc`, test colocated as `<unit>_test.cc`.

### Bazel targets

- `cc_library` targets are suffixed `_cc` (e.g. `command_cc`).
- `cc_test` targets are suffixed `_test`, `size = "small"` by default.
- Split `implementation_deps` (private, not propagated) from `deps` (public API
  surface).
- Package `default_visibility = ["//visibility:private"]`; widen explicitly per
  target only where a public boundary is intended.

## Protobuf

- Edition 2024 (`edition = "2024";`). `features.field_presence = EXPLICIT` only
  where set-vs-empty matters. See [CARVE_DESIGN.md](CARVE_DESIGN.md) section 4.4.

## Shell and Starlark

- Shell: Google shell style, formatted with `shfmt`, linted with `shellcheck`.
- Starlark/Bazel: formatted and linted with `buildifier` (`--warnings=all`).

## Markdown

- Tables are vertically aligned: every column padded to a consistent width,
  pipes lined up, the `---` separator row matched, `:---`/`---:` markers kept.
  Enforced by `tools/align_md_tables.py` via the `align-md-tables` pre-commit
  hook; run `pre-commit run align-md-tables -a` (or the script directly) to fix.

## Tests

- GTest, colocated with the unit. Every change is covered by a committed test at
  the appropriate level - see the testing-discipline section in
  [AGENTS.md](AGENTS.md#testing-discipline). No exemption category.
- **Assert with matchers.** Prefer `EXPECT_THAT(actual, matcher)` over
  `EXPECT_EQ`/`EXPECT_NE`. Use the expressive matcher, not a hand-rolled
  predicate:
  - substring: `EXPECT_THAT(text, HasSubstr("x"))` - never
    `EXPECT_NE(text.find("x"), npos)`.
  - equality: `Eq(...)` / `StrEq(...)`; containers: `ElementsAre`, `IsEmpty`,
    `SizeIs`; structs: `Field(&T::member, matcher)` with `AllOf` for several.
  - status: the `mbo::testing` matchers (`mbo/testing/status.h`,
    `@helly25_mbo//mbo/testing:status_cc`) `IsOk()`, `StatusIs(code)`,
    `IsOkAndHolds(value_matcher)`; `::absl_testing::` is disallowed (see STYLE_CPP.md
    "Status matchers"). Use `IsOkAndHolds(m)` rather than asserting `IsOk()` and then
    dereferencing (`*x`) to compare the value.
