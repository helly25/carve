#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) The helly25/carve authors (github.com/helly25/carve)
# SPDX-License-Identifier: Apache-2.0
"""Differential harness for compilation databases.

Compares two ``compile_commands.json`` files entry-by-entry and reports the
meaningful differences, ignoring per-build / per-host volatile tokens (output
paths, dependency files, random seeds, compilation-dir stamps). Use it to check
carve's CDB against a reference extractor's (e.g. Hedron's). See CARVE_DESIGN.md
sections 8 and 9.4.

    tools/cdb_diff.py carve.json reference.json
    tools/cdb_diff.py --selftest          # run the built-in checks

Entries are keyed by their workspace-relative source path (the tail after an
``execroot/<ws>`` segment), so a CDB with absolute execroot paths and one with
workspace-relative paths line up. For shared sources, the normalized *flag sets*
are compared (order-insensitive), with volatile flags removed.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import OrderedDict

# Flags whose joined `<flag>=<value>` form is per-build/per-host noise.
_VOLATILE_PREFIXES = (
    "-frandom-seed=",
    "-fdebug-prefix-map=",
    "-ffile-compilation-dir=",
    "-fdebug-compilation-dir=",
    "-fcoverage-compilation-dir=",
)

# Flags that take the following token as a value; both are volatile (the object
# file, the dependency file, the dependency target).
_VOLATILE_WITH_VALUE = ("-o", "-MF", "-MT", "-MQ")


def workspace_relative(path: str) -> str:
    """Returns `path` relative to the Bazel workspace, for cross-CDB keying."""
    norm = path.replace("\\", "/")
    for marker in ("/execroot/", "/sandbox/"):
        idx = norm.rfind(marker)
        if idx != -1:
            # Drop the marker and the single workspace-name segment after it.
            tail = norm[idx + len(marker) :].split("/", 1)
            return tail[1] if len(tail) == 2 else tail[0]
    return norm.lstrip("/")


def normalized_flags(arguments: list[str]) -> set[str]:
    """Returns the set of flags in `arguments` with volatile tokens removed."""
    flags: set[str] = set()
    skip_next = False
    for i, arg in enumerate(arguments):
        if skip_next:
            skip_next = False
            continue
        if i == 0:  # argv[0] is the compiler path; not a flag.
            continue
        if arg in _VOLATILE_WITH_VALUE:
            skip_next = True
            continue
        if arg.startswith(_VOLATILE_PREFIXES):
            continue
        flags.add(arg)
    return flags


def index_by_source(entries: list[dict]) -> "OrderedDict[str, dict]":
    """Maps each entry's workspace-relative source to the entry (last wins)."""
    by_source: "OrderedDict[str, dict]" = OrderedDict()
    for entry in entries:
        by_source[workspace_relative(entry["file"])] = entry
    return by_source


def diff(left: list[dict], right: list[dict]) -> dict:
    """Diffs two CDB entry lists; returns a structured result."""
    left_by, right_by = index_by_source(left), index_by_source(right)
    left_only = sorted(set(left_by) - set(right_by))
    right_only = sorted(set(right_by) - set(left_by))
    flag_diffs = []
    for source in sorted(set(left_by) & set(right_by)):
        left_flags = normalized_flags(left_by[source]["arguments"])
        right_flags = normalized_flags(right_by[source]["arguments"])
        only_left = sorted(left_flags - right_flags)
        only_right = sorted(right_flags - left_flags)
        if only_left or only_right:
            flag_diffs.append({"source": source, "only_left": only_left, "only_right": only_right})
    return {
        "left_count": len(left_by),
        "right_count": len(right_by),
        "common": len(set(left_by) & set(right_by)),
        "left_only": left_only,
        "right_only": right_only,
        "flag_diffs": flag_diffs,
    }


def format_report(result: dict, left_name: str, right_name: str) -> str:
    lines = [
        f"# CDB diff: {left_name} (left) vs {right_name} (right)",
        "",
        f"- left entries:  {result['left_count']}",
        f"- right entries: {result['right_count']}",
        f"- shared sources: {result['common']}",
        f"- only in left:  {len(result['left_only'])}",
        f"- only in right: {len(result['right_only'])}",
        f"- sources with flag differences: {len(result['flag_diffs'])}",
    ]
    for source in result["left_only"]:
        lines.append(f"  only-left source: {source}")
    for source in result["right_only"]:
        lines.append(f"  only-right source: {source}")
    for entry in result["flag_diffs"]:
        lines.append(f"\n## {entry['source']}")
        for flag in entry["only_left"]:
            lines.append(f"  - left-only: {flag}")
        for flag in entry["only_right"]:
            lines.append(f"  + right-only: {flag}")
    return "\n".join(lines)


def _load(path: str) -> list[dict]:
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def _selftest() -> int:
    # Keying lines up an absolute execroot path with a workspace-relative one.
    left = [
        {
            "file": "/home/u/.cache/bazel/x/execroot/_main/pkg/a.cc",
            "directory": "/home/u/.cache/bazel/x/execroot/_main",
            "arguments": ["clang", "-c", "pkg/a.cc", "-o", "a.o", "-Iinclude", "-DLEFT", "-frandom-seed=a.o"],
        }
    ]
    right = [
        {
            "file": "pkg/a.cc",
            "directory": "/home/u/proj",
            "arguments": ["clang", "-c", "pkg/a.cc", "-o", "b.o", "-Iinclude", "-DRIGHT", "-MF", "a.d"],
        },
        {"file": "pkg/b.cc", "directory": "/home/u/proj", "arguments": ["clang", "-c", "pkg/b.cc"]},
    ]
    result = diff(left, right)
    assert result["common"] == 1, result
    assert result["left_only"] == [], result
    assert result["right_only"] == ["pkg/b.cc"], result
    # Volatile tokens (-o values, -frandom-seed, -MF value) are ignored; only the
    # real flag difference (-DLEFT vs -DRIGHT) survives.
    assert len(result["flag_diffs"]) == 1, result
    only = result["flag_diffs"][0]
    assert only["only_left"] == ["-DLEFT"], only
    assert only["only_right"] == ["-DRIGHT"], only
    # workspace_relative strips the execroot + workspace-name segment.
    assert workspace_relative("/x/execroot/_main/pkg/a.cc") == "pkg/a.cc"
    assert workspace_relative("pkg/a.cc") == "pkg/a.cc"
    print("cdb_diff selftest: OK")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Diff two compile_commands.json files.")
    parser.add_argument("left", nargs="?", help="left compile_commands.json (e.g. carve's)")
    parser.add_argument("right", nargs="?", help="right compile_commands.json (e.g. a reference)")
    parser.add_argument("--selftest", action="store_true", help="run built-in checks and exit")
    args = parser.parse_args(argv)
    if args.selftest:
        return _selftest()
    if not args.left or not args.right:
        parser.error("left and right compile_commands.json are required (or pass --selftest)")
    print(format_report(diff(_load(args.left), _load(args.right)), args.left, args.right))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
