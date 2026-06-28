# SPDX-FileCopyrightText: Copyright (c) The helly25/carve authors (github.com/helly25/carve)
# SPDX-License-Identifier: Apache-2.0
"""Layer B: a `bazel run` target that refreshes the compilation database.

`carve_refresh` wraps the `carve` binary in a runnable target:

    load("@helly25_carve//rules:carve.bzl", "carve_refresh")

    carve_refresh(
        name = "refresh",
        targets = ["//..."],
        project_id = "main",
    )

`bazel run //:refresh` writes `compile_commands.json` to the workspace root.

It is a **run** target, not a build action. carve invokes `bazel aquery` and
`bazel info`; spawning a second bazel inside a build action is the nested-bazel
trap (lock/server contention, sandboxing). Running carve *after* the outer build,
via `bazel run`, keeps the server free. (CARVE_DESIGN.md sections 3.1, 4.6.)
"""

load(":cc_carve_aspect.bzl", "CarveShardsInfo", "cc_carve_aspect")

def _carve_refresh_impl(ctx):
    carve = ctx.executable.carve
    launcher = ctx.actions.declare_file(ctx.label.name + ".sh")
    ctx.actions.expand_template(
        template = ctx.file._launcher,
        output = launcher,
        is_executable = True,
        substitutions = {
            "@@CARVE_RLOCATION@@": "{}/{}".format(ctx.workspace_name, carve.short_path),
            "@@TARGETS@@": ",".join(ctx.attr.targets),
            "@@OUTPUT@@": ctx.attr.output,
            "@@SIDECAR@@": ctx.attr.sidecar,
            "@@PROJECT_ID@@": ctx.attr.project_id,
        },
    )
    runfiles = ctx.runfiles(files = [carve]).merge_all([
        ctx.attr.carve[DefaultInfo].default_runfiles,
        ctx.attr._bash_runfiles[DefaultInfo].default_runfiles,
    ])
    return [DefaultInfo(executable = launcher, runfiles = runfiles)]

carve_refresh = rule(
    implementation = _carve_refresh_impl,
    executable = True,
    doc = "A `bazel run` target that writes `compile_commands.json` for `targets`.",
    attrs = {
        "targets": attr.string_list(
            default = ["//..."],
            doc = "Target patterns to aquery (passed as args, not deps, so the whole graph is not analyzed).",
        ),
        "output": attr.string(
            default = "compile_commands.json",
            doc = "Compilation-database path, relative to the workspace root.",
        ),
        "sidecar": attr.string(
            default = ".carve-cache/entries-by-actionkey.binpb",
            doc = "Incremental-refresh sidecar path, relative to the workspace root.",
        ),
        "project_id": attr.string(
            default = "",
            doc = "Project id; scopes the sidecar merge for a shared cross-project database.",
        ),
        "carve": attr.label(
            default = Label("//carve:carve"),
            executable = True,
            cfg = "target",
            doc = "The carve binary to run.",
        ),
        "_launcher": attr.label(default = Label("//rules:carve_refresh.tpl.sh"), allow_single_file = True),
        "_bash_runfiles": attr.label(default = Label("@bazel_tools//tools/bash/runfiles")),
    },
)

def _carve_aspect_refresh_impl(ctx):
    carve = ctx.executable.carve

    # cc_carve_aspect ran over each target's transitive cc graph and produced one
    # shard per compile action; gather them all.
    shards = depset(transitive = [t[CarveShardsInfo].shards for t in ctx.attr.targets])
    shard_list = shards.to_list()

    # The launcher resolves shards from runfiles via rlocation; hand it the keys
    # (one `<workspace>/<short_path>` per line) rather than baking N paths into
    # the template.
    manifest = ctx.actions.declare_file(ctx.label.name + ".shards_manifest")
    ctx.actions.write(
        manifest,
        "".join(["{}/{}\n".format(ctx.workspace_name, shard.short_path) for shard in shard_list]),
    )

    launcher = ctx.actions.declare_file(ctx.label.name + ".sh")
    ctx.actions.expand_template(
        template = ctx.file._launcher,
        output = launcher,
        is_executable = True,
        substitutions = {
            "@@CARVE_RLOCATION@@": "{}/{}".format(ctx.workspace_name, carve.short_path),
            "@@MANIFEST_RLOCATION@@": "{}/{}".format(ctx.workspace_name, manifest.short_path),
            "@@OUTPUT@@": ctx.attr.output,
            "@@PROJECT_ID@@": ctx.attr.project_id,
        },
    )
    runfiles = ctx.runfiles(files = [carve, manifest] + shard_list).merge_all([
        ctx.attr.carve[DefaultInfo].default_runfiles,
        ctx.attr._bash_runfiles[DefaultInfo].default_runfiles,
    ])
    return [DefaultInfo(
        executable = launcher,
        # Building this target builds every shard (each a cacheable CarveShard
        # action), so a plain `bazel build` (and `build_test`) exercises the whole
        # Layer C shard-production path.
        files = depset([launcher] + shard_list),
        runfiles = runfiles,
    )]

carve_aspect_refresh = rule(
    implementation = _carve_aspect_refresh_impl,
    executable = True,
    doc = """Layer C: build per-action shards via `cc_carve_aspect`, then `bazel run` to aggregate them.

`bazel run //:refresh_aspect` first builds one cacheable shard per compile action
across `targets` (so an edit re-shards only what changed), then merges the shards
into `compile_commands.json`. Unlike `carve_refresh`, `targets` are real label
dependencies (the aspect must analyze them), trading whole-graph analysis for
per-action build-cache incrementality (CARVE_DESIGN.md section 4.7).""",
    attrs = {
        "targets": attr.label_list(
            aspects = [cc_carve_aspect],
            doc = "cc targets whose transitive compile actions are sharded (real deps, so the aspect runs).",
        ),
        "output": attr.string(
            default = "compile_commands.json",
            doc = "Compilation-database path, relative to the workspace root.",
        ),
        "project_id": attr.string(
            default = "",
            doc = "Project id stamped on the aggregated database.",
        ),
        "exclude_external_sources": attr.bool(
            default = True,
            doc = "Shard only first-party (main-repo) compile actions, skipping targets " +
                  "from external repositories: clangd resolves their headers via " +
                  "first-party entries' -I flags, so they need no entries of their own. " +
                  "Set False to shard the full transitive graph.",
        ),
        "record_headers": attr.bool(
            default = False,
            doc = "Record each compile action's header dependencies in its shard " +
                  "(source_kind ASPECT_M), for a shard-built header index. When set, each " +
                  "shard consumes the compile's own `.d` dependency file for the exact " +
                  "#include set -- coupling sharding to building every TU, so the default " +
                  "(False) keeps shards build-free.",
        ),
        "carve": attr.label(
            default = Label("//carve:carve_aggregate"),
            executable = True,
            cfg = "target",
            doc = "Binary run to merge the shards. Defaults to the lean, LLVM-free " +
                  "//carve:carve_aggregate; override with //carve:carve for the full binary.",
        ),
        "_launcher": attr.label(default = Label("//rules:carve_aggregate.tpl.sh"), allow_single_file = True),
        "_bash_runfiles": attr.label(default = Label("@bazel_tools//tools/bash/runfiles")),
    },
)
