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

"""Vertically align Markdown table columns in-place.

Pads every cell (header, body, and the `---` separator row) so each column is a
consistent width and the pipes line up. Alignment markers (`:---`, `---:`,
`:---:`) are preserved. Fenced code blocks (``` ... ```) are left untouched.
Idempotent: running twice produces no further change.

Usage: align_md_tables.py FILE [FILE ...]   (rewrites files; exit 0)

Column widths are computed by character count, which keeps the *source* aligned;
double-width glyphs (e.g. emoji) may still render slightly off but the file reads
cleanly in diffs. Run from the repo root, e.g. `python3 tools/align_md_tables.py
$(git ls-files '*.md')`.
"""

import re
import sys

_SEP_CELL = re.compile(r":?-+:?")


def _split_cells(line: str) -> list[str]:
    s = line.strip()
    if s.startswith("|"):
        s = s[1:]
    if s.endswith("|"):
        s = s[:-1]
    return [c.strip() for c in s.split("|")]


def _is_separator(cells: list[str]) -> bool:
    return bool(cells) and all(_SEP_CELL.fullmatch(c) for c in cells)


def _align_block(rows: list[str]) -> list[str]:
    grid = [_split_cells(r) for r in rows]
    ncol = max(len(r) for r in grid)
    for r in grid:
        r.extend([""] * (ncol - len(r)))
    widths = [0] * ncol
    for r in grid:
        if _is_separator(r):
            continue
        for i, c in enumerate(r):
            widths[i] = max(widths[i], len(c))
    out = []
    for r in grid:
        if _is_separator(r):
            seg = []
            for i in range(ncol):
                left = r[i].startswith(":")
                right = r[i].endswith(":")
                dashes = "-" * max(3, widths[i])
                seg.append((":" if left else "") + dashes + (":" if right else ""))
            out.append("| " + " | ".join(seg) + " |")
        else:
            out.append("| " + " | ".join(c.ljust(widths[i]) for i, c in enumerate(r)) + " |")
    return out


def align(text: str) -> str:
    lines = text.split("\n")
    result: list[str] = []
    in_fence = False
    i = 0
    while i < len(lines):
        stripped = lines[i].lstrip()
        if stripped.startswith("```") or stripped.startswith("~~~"):
            in_fence = not in_fence
            result.append(lines[i])
            i += 1
            continue
        if not in_fence and lines[i].lstrip().startswith("|"):
            j = i
            while j < len(lines) and lines[j].lstrip().startswith("|"):
                j += 1
            block = lines[i:j]
            if any(_is_separator(_split_cells(b)) for b in block):
                result.extend(_align_block(block))
            else:
                result.extend(block)
            i = j
            continue
        result.append(lines[i])
        i += 1
    return "\n".join(result)


def main(paths: list[str]) -> int:
    for path in paths:
        with open(path, encoding="utf-8") as handle:
            original = handle.read()
        aligned = align(original)
        if aligned != original:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(aligned)
            print(f"aligned {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
