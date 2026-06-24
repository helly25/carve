# SPDX-FileCopyrightText: Copyright (c) The helly25/carve authors (github.com/helly25/carve)
# SPDX-License-Identifier: Apache-2.0
"""Layer C: an aspect that emits one cacheable shard per compile action.

`cc_carve_aspect` walks the cc graph and, for every C++ compile action it finds,
schedules a `carve shard` build action that writes a one-record shard (an
`ActionRecords` binary proto). The shards are collected in the `carve_shards`
output group and merged into a compilation database by `carve aggregate` (see
`carve_aspect_refresh` in carve.bzl).

The compile command is read straight from `action.argv`: Bazel hands back the
fully-expanded driver command line (param files already expanded, no `@file`
indirection), which is exactly what `carve shard` de-Bazels into a database entry.

A shard's content is a function of the compile command alone, so each shard
action's only input is its `command_file`. Bazel's action cache then re-runs an
individual shard exactly when that translation unit's *command* changes (a new
flag, define, or dependency) — the per-action incrementality Layers A/B cannot
offer (CARVE_DESIGN.md sections 3.1, 4.7). Editing source/header *content* leaves
the command, and therefore the database entry, unchanged, so no re-shard is
needed (clangd re-reads the changed files itself). Shards are therefore not
scanned for headers (`--scan=false`): the database does not use headers, and
delegating invalidation to Bazel avoids scanning every TU inside a build action.
"""

load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

# The compile actions worth a shard. A translation unit (real source) produces a
# compilation-database entry; header-only `parse_headers` actions do not (their
# "source" is a header, so `carve shard` would find no TU anyway).
_COMPILE_MNEMONICS = ["CppCompile", "ObjcCompile", "CppModuleCompile"]

# cc graph edges the aspect propagates along, so a single top-level target shards
# its whole transitive closure.
_PROPAGATE_ATTRS = ["deps", "implementation_deps", "interface_deps"]

CarveShardsInfo = provider(
    doc = "Transitive set of per-action shard files produced by cc_carve_aspect.",
    fields = {"shards": "depset of shard files (ActionRecords binary protos)"},
)

def _find_source(argv):
    """Returns the compile source — the token after `-c` — or "" if none."""
    for i in range(len(argv) - 1):
        if argv[i] == "-c":
            return argv[i + 1]
    return ""

def _cc_carve_aspect_impl(target, ctx):
    direct = []
    if CcInfo in target:
        index = 0
        for action in target.actions:
            if action.mnemonic not in _COMPILE_MNEMONICS:
                continue
            argv = action.argv
            source = _find_source(argv)
            if not source:
                continue
            base = "{}.carve.{}".format(target.label.name, index)
            index += 1

            command_file = ctx.actions.declare_file(base + ".cmd")
            ctx.actions.write(command_file, "\n".join(argv) + "\n")

            shard = ctx.actions.declare_file(base + ".shard.binpb")
            outputs = action.outputs.to_list()
            primary_output = outputs[0].path if outputs else ""

            args = ctx.actions.args()
            args.add("shard")
            args.add("--action_key", "{} {}".format(target.label, source))
            args.add("--command_file", command_file)
            args.add("--source", source)
            args.add("--out", shard)
            args.add("--scan=false")  # Layer C does not record headers; see the module docstring.
            if primary_output:
                args.add("--primary_output", primary_output)

            ctx.actions.run(
                executable = ctx.executable._carve,
                arguments = [args],
                # The shard records the (de-Bazeled) command + source; its content
                # depends only on `command_file`, so that is the sole input. Bazel
                # re-runs this shard exactly when the compile command changes (a new
                # flag/dep) — header/source *content* edits leave the command (and
                # thus the database entry) unchanged, so no re-shard is needed.
                inputs = depset([command_file]),
                outputs = [shard],
                mnemonic = "CarveShard",
                progress_message = "Carving shard for %s" % source,
            )
            direct.append(shard)

    transitive = []
    for attr_name in _PROPAGATE_ATTRS:
        for dep in getattr(ctx.rule.attr, attr_name, None) or []:
            if CarveShardsInfo in dep:
                transitive.append(dep[CarveShardsInfo].shards)

    shards = depset(direct, transitive = transitive)
    return [CarveShardsInfo(shards = shards), OutputGroupInfo(carve_shards = shards)]

cc_carve_aspect = aspect(
    implementation = _cc_carve_aspect_impl,
    attr_aspects = _PROPAGATE_ATTRS,
    doc = "Emits one cacheable `carve shard` per compile action across the cc graph.",
    attrs = {
        "_carve": attr.label(
            default = Label("//carve:carve"),
            executable = True,
            cfg = "exec",
            doc = "The carve binary, run once per compile action to produce a shard.",
        ),
    },
)
