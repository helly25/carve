# SPDX-FileCopyrightText: Copyright (c) The helly25/carve authors (github.com/helly25/carve)
# SPDX-License-Identifier: Apache-2.0
"""Layer C: an aspect that emits one cacheable shard per compile action.

`cc_carve_aspect` walks the cc graph and, for every C++ compile action it finds,
schedules a `carve_shard` build action that writes a one-record shard (an
`ActionRecords` binary proto). The shards are collected in the `carve_shards`
output group and merged into a compilation database by `carve aggregate` (see
`carve_aspect_refresh` in carve.bzl).

The tool is `carve_shard`, not the full `carve` binary: it is the lean, scan-free
build of the `shard` subcommand and links neither scan_deps nor the from-source
LLVM, so this per-compile-action build tool stays cheap to build (a full
LLVM-linked exec tool per action would be untenable).

The compile command is read straight from `action.argv`: Bazel hands back the
fully-expanded driver command line (param files already expanded, no `@file`
indirection), which is exactly what `carve_shard` de-Bazels into a database entry.

A shard's content is a function of the compile command alone, so each shard
action's only input is its `command_file`. Bazel's action cache then re-runs an
individual shard exactly when that translation unit's *command* changes (a new
flag, define, or dependency) - the per-action incrementality Layers A/B cannot
offer (CARVE_DESIGN.md sections 3.1, 4.7). Editing source/header *content* leaves
the command, and therefore the database entry, unchanged, so no re-shard is
needed (clangd re-reads the changed files itself). By default a shard records no
headers (`carve_shard` links no scanner), and delegating invalidation to Bazel
avoids touching every TU. The opt-in `record_headers` parameter feeds each shard
the compile's own dependency file (`-MF .../x.d`) and records the exact #include
set (ASPECT_M) for a shard-built header index, at the cost of building the TUs
(whose `.d` files it reuses).
"""

load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

# The compile actions worth a shard. A translation unit (real source) produces a
# compilation-database entry; header-only `parse_headers` actions do not (their
# "source" is a header, so `carve_shard` would find no TU anyway).
_COMPILE_MNEMONICS = ["CppCompile", "ObjcCompile", "CppModuleCompile"]

# cc graph edges the aspect propagates along, so a single top-level target shards
# its whole transitive closure.
_PROPAGATE_ATTRS = ["deps", "implementation_deps", "interface_deps"]

CarveShardsInfo = provider(
    doc = "Transitive set of per-action shard files produced by cc_carve_aspect.",
    fields = {"shards": "depset of shard files (ActionRecords binary protos)"},
)

def _find_source(argv):
    """Returns the compile source - the token after `-c` - or "" if none."""
    for i in range(len(argv) - 1):
        if argv[i] == "-c":
            return argv[i + 1]
    return ""

def _cc_carve_aspect_impl(target, ctx):
    direct = []

    # Layer C builds a compilation database for first-party code. clangd resolves
    # external headers through the `-I` flags carried on first-party entries, so
    # external libraries need no entries of their own; sharding them only bloats
    # the database (and the build) with sources nobody edits. Skip them by default.
    # Propagation still descends through external targets (attr_aspects), so a
    # first-party target reached only via an external edge is not lost.
    is_external = target.label.workspace_name != ""
    shard_target = CcInfo in target and not (ctx.attr.exclude_external_sources and is_external)
    if shard_target:
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

            # carve_shard takes the shard flags directly (no subcommand).
            args = ctx.actions.args()
            args.add("--action_key", "{} {}".format(target.label, source))
            args.add("--command_file", command_file)
            args.add("--source", source)
            args.add("--out", shard)
            if primary_output:
                args.add("--primary_output", primary_output)

            # A shard's content is a function of its compile command, so the command
            # file is its only input by default: Bazel re-runs a shard exactly when
            # that command changes (a new flag/dep), while header/source *content*
            # edits leave the command (and the database entry) unchanged.
            #
            # With record_headers, the shard also consumes the compile's own
            # dependency file (`-MF .../x.d` -- the make-format `-M` output Bazel
            # already generates for include validation). Declaring it as an input
            # makes Bazel run the compile, and carve_shard parses it for the exact
            # #include set (ASPECT_M). That couples the shard to building its TU --
            # negating the build-free property -- so it is opt-in (default off).
            # (Re-running the compiler standalone with `-M` is unreliable: the driver
            # is often a wrapper and already carries its own `-MD/-MF`.)
            shard_inputs = [command_file]
            if ctx.attr.record_headers:
                depfiles = [out for out in action.outputs.to_list() if out.extension == "d"]
                if depfiles:
                    args.add("--depfile", depfiles[0])
                    shard_inputs.append(depfiles[0])

            ctx.actions.run(
                executable = ctx.executable._carve_shard,
                arguments = [args],
                inputs = depset(shard_inputs),
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
    doc = "Emits one cacheable shard per compile action across the cc graph.",
    attrs = {
        "_carve_shard": attr.label(
            default = Label("//carve:carve_shard"),
            executable = True,
            cfg = "exec",
            doc = "The lean carve_shard tool, run once per compile action to produce a shard.",
        ),
        # Parameters: supplied by the propagating rule's same-named attributes.
        "exclude_external_sources": attr.bool(default = True),
        "record_headers": attr.bool(default = False),
    },
)
