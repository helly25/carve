# AGENTS.md

Conventions for AI coding agents (and humans) working on `helly25/carve`. This
file is the canonical source; tool-specific entry points such as
[CLAUDE.md](CLAUDE.md) should reference it rather than duplicate.

For architecture, layering, and the build-out plan, see
[CARVE_DESIGN.md](CARVE_DESIGN.md).

## File headers

Every Bazel, Starlark, C++, proto, and shell file in the repo starts with:

```
# SPDX-FileCopyrightText: Copyright (c) The helly25/carve authors (github.com/helly25/carve)
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
```

For C++ / proto / other `//`-comment languages, use `//` instead of `#`.

Markdown, JSON, and other formats that do not support comments are exempt.

## Library priority

When picking where a capability lives, prefer in this order:

1. **Standard C++23** if usable out of the box.
2. **Abseil** if usable out of the box.
3. **[helly25/mbo](https://github.com/helly25/mbo)** as a wrapper or extension
   when std or Abseil need smoothing for our use.

Anything mbo lacks that we need gets added upstream there, not locally in
`carve`. New utility helpers default to a contribution to mbo unless they are
unambiguously specific to compile-commands extraction.

**No Boost.** Anywhere.

## Code provenance

Carve is a clean-slate rewrite. Do not copy code from
`bazel-compile-commands-extractor` or other compile-commands tools. Inventories
of platform quirks and rederivation notes are fine; specific code expressions
are not. See section 4.3 of [CARVE_DESIGN.md](CARVE_DESIGN.md) for the primary
sources to rederive from.

## C++

Code-style and structural rules (layout, namespaces, header guards, target
naming, `deps` vs `implementation_deps`) live in [RULES.md](RULES.md). Summary:

- C++23, clang 20.1+ (pinned at LLVM 22.1.7 via the hermetic-llvm `llvm` module).
- Style: Google C++ with deviations in [.clang-format](.clang-format).
- Lints: [.clang-tidy](.clang-tidy), `WarningsAsErrors: true`.
- No exceptions (`-fno-exceptions` is set in `.bazelrc`); use `absl::Status` /
  `absl::StatusOr` / `std::expected` for errors.
- Layout: one module per `carve/<module>/` package, `namespace carve::<module>`,
  `<module>_cc` library + colocated `<module>_test` (`<unit>_test.cc`).

## Testing discipline

Write relevant tests at **all** levels - unit, golden (per de-Bazeling quirk),
integration (synthetic workspaces under `testdata/`), and the differential
harness against the existing tool. See [CARVE_DESIGN.md](CARVE_DESIGN.md)
section 9 for the layout.

Do not treat ad-hoc, one-shot verification (running `carve` by hand, a throwaway
script, a manual diff) as a substitute for tests, and do not run such a check
and then move on. For any change, make a deliberate choice up front:

1. **Write the real test(s) outright** when the behavior is specifiable now, or
2. **Use the one-shot experiment as planning input** - capture what it told you
   (inputs, expected output, edge cases) and turn it into a committed test
   before the change is considered done.

A change is not complete until the behavior it introduces or fixes is covered by
a committed test at the appropriate level(s). One-shot exploration informs the
test plan; it never replaces it.

There is no exemption category. "It's just a refactor / a one-liner / hard to
test" is not a reason to skip - it is a reason to pick the right level (a
behavior-preserving change is covered by the tests that already pin that
behavior; if none exist, that gap is part of the change). If a behavior
genuinely cannot be tested, say so explicitly in the PR and explain why; do not
silently ship it untested.

## Python

Minimize. The runtime tool is C++. Python is allowed for developer-only
helpers (e.g. flag table regeneration) and only on Python 3.13+.

## Protobuf

Edition 2024 (`edition = "2024";`) for new `.proto` files. Use
`features.field_presence = EXPLICIT` only where set-vs-empty actually matters.

## Build

```bash
bazel build //...                  # default toolchain
bazel build //... --config=clang   # hermetic clang via the llvm (hermetic-llvm) module
bazel test //... --config=asan     # sanitizer presets
```

## Commits

- All commits signed off (DCO): `git commit -s`.
- Commit message style: short imperative subject (under 70 chars), optional
  body with the why.
- One logical change per commit. Mixing refactors with feature work is
  discouraged.
- Do not amend pushed commits; create a fresh commit instead.

## Pull requests

- Branches off `main`. PRs target `main`.
- Description explains the why; the diff explains the what.
- All CI green before merge.
- Squash-merge by default; preserve commit history only for substantial,
  well-organized series.

## Filing changes as an agent

If you are an AI agent making changes:

1. Read [CARVE_DESIGN.md](CARVE_DESIGN.md) and this file before writing code - and
   [RULES.md](RULES.md) plus [STYLE_CPP.md](STYLE_CPP.md) before writing or changing
   C++ (structure + the detailed style: matchers, `// NL`, idioms).
2. State the change you intend to make in plain language before editing.
3. Run `bazel build //...` and `bazel test //...` locally before declaring a
   change complete. If tests cannot be run in your environment, say so.
4. Do not introduce new dependencies without explicit human approval.
5. Do not silently update version pins in `MODULE.bazel`.
6. Prefer additive changes. Refactors that touch many files should be split.
7. Make conservative edits. Prefer `Edit` over `Write` for surgical changes;
   reserve `Write` for full rewrites and brand-new files. When iterating on a
   feature, edit the relevant existing section in place rather than appending a
   new one at the end. Do not create files, add dependencies, or add
   abstractions or speculative features the design does not call for.
8. Surface conflicts. When [CARVE_DESIGN.md](CARVE_DESIGN.md) (or another design
   doc) and the code disagree, surface the conflict in your response rather than
   silently choosing one.

## Documents to keep in sync

- [CARVE_DESIGN.md](CARVE_DESIGN.md): architecture. Update when design changes.
- [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md): status + ordered remaining work. Update as milestones land.
- [RULES.md](RULES.md): code-style and structural rules. Update when conventions change.
- [STYLE_CPP.md](STYLE_CPP.md): detailed C++ style (shared helly25 conventions with examples); the companion RULES.md points to.
- [README.md](README.md): user-facing intro. Update on user-visible changes.
- [CHANGELOG.md](CHANGELOG.md): every PR adds a line under `[Unreleased]`.
- [CONTRIBUTING.md](CONTRIBUTING.md): human onboarding. Should stay short.
