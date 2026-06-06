# RULES.md

Code-style and structural rules for `helly25/carve`. These mirror the helly25
house style (see sibling repos [mbo](https://github.com/helly25/mbo) and
[bzl](https://github.com/helly25/bzl)).

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

- C++23, clang 20.1+ (clang 22.x recommended pin via `toolchains_llvm`).
- Style: Google C++ with the deviations in [.clang-format](.clang-format);
  formatting is enforced, not negotiated.
- Lints: [.clang-tidy](.clang-tidy) with `WarningsAsErrors: true`.
- No exceptions (`-fno-exceptions`); errors flow through `absl::Status`,
  `absl::StatusOr`, or `std::expected`.

### Layout and naming

- **One module per directory:** `carve/<module>/`, self-contained.
- **Namespaces:** `carve::<module>`. Internal-only code lives in
  `carve::<module>::<module>_internal` — never a bare `internal` namespace.
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

## Tests

- GTest, colocated with the unit. Every change is covered by a committed test at
  the appropriate level — see the testing-discipline section in
  [AGENTS.md](AGENTS.md#testing-discipline). No exemption category.
- **Assert with matchers.** Prefer `EXPECT_THAT(actual, matcher)` over
  `EXPECT_EQ`/`EXPECT_NE`. Use the expressive matcher, not a hand-rolled
  predicate:
  - substring: `EXPECT_THAT(text, HasSubstr("x"))` — never
    `EXPECT_NE(text.find("x"), npos)`.
  - equality: `Eq(...)` / `StrEq(...)`; containers: `ElementsAre`, `IsEmpty`,
    `SizeIs`; structs: `Field(&T::member, matcher)` with `AllOf` for several.
  - status: `absl_testing::IsOk()`, `StatusIs(code)`, and
    `IsOkAndHolds(value_matcher)`. Use `IsOkAndHolds(m)` rather than asserting
    `IsOk()` and then dereferencing (`*x`) to compare the value.
