# Claude Code - carve

This repository's agent rules ([`AGENTS.md`](AGENTS.md)) and C++ coding style
([`STYLE_CPP.md`](STYLE_CPP.md)) are **binding**. Read and follow both before
writing or reviewing code; `STYLE_CPP.md` is canonical for C++ (it covers, among
much else: `absl::Mutex` + full thread-safety annotations, `absl::StatusOr` and
the `MBO_*` status macros in production and tests, and gmock matchers). AGENTS.md
is the single source of truth for project conventions; CLAUDE.md exists only to
surface it to tools that look for `CLAUDE.md` by name.

For architecture, layering, and the build-out plan, see
[CARVE_DESIGN.md](CARVE_DESIGN.md).

The files below are imported so they are always in context:

@AGENTS.md
@STYLE_CPP.md
