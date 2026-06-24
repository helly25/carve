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
