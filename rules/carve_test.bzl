# SPDX-FileCopyrightText: Copyright (c) The helly25/carve authors (github.com/helly25/carve)
# SPDX-License-Identifier: Apache-2.0
"""Analysis tests for `carve_refresh` (rules/carve.bzl).

These run in `bazel test` without executing the target, so they validate the
rule's wiring (it is runnable and carries the carve binary in its runfiles)
without the nested-bazel trap that running `carve refresh` inside a test would
hit. The end-to-end behavior is dogfooded via `bazel run //:refresh`.
"""

load("@bazel_skylib//lib:unittest.bzl", "analysistest", "asserts")
load("@bazel_skylib//rules:build_test.bzl", "build_test")
load(":carve.bzl", "carve_aspect_refresh", "carve_refresh")

def _refresh_wiring_test_impl(ctx):
    env = analysistest.begin(ctx)
    target = analysistest.target_under_test(env)
    asserts.true(
        env,
        target[DefaultInfo].files_to_run.executable != None,
        "carve_refresh should produce a runnable launcher",
    )
    runfiles = [f.short_path for f in target[DefaultInfo].default_runfiles.files.to_list()]
    asserts.true(
        env,
        [p for p in runfiles if p.endswith("carve/carve")] != [],
        "the carve binary should be in the launcher's runfiles, got: {}".format(runfiles),
    )
    return analysistest.end(env)

_refresh_wiring_test = analysistest.make(_refresh_wiring_test_impl)

def _aspect_refresh_wiring_test_impl(ctx):
    env = analysistest.begin(ctx)
    target = analysistest.target_under_test(env)
    asserts.true(
        env,
        target[DefaultInfo].files_to_run.executable != None,
        "carve_aspect_refresh should produce a runnable launcher",
    )
    runfiles = [f.short_path for f in target[DefaultInfo].default_runfiles.files.to_list()]
    asserts.true(
        env,
        [p for p in runfiles if p.endswith("carve/carve_aggregate")] != [],
        "the lean carve_aggregate binary should be in the launcher's runfiles, got: {}".format(runfiles),
    )
    asserts.true(
        env,
        [p for p in runfiles if p.endswith(".shard.binpb")] != [],
        "the aspect should have produced at least one shard in the runfiles, got: {}".format(runfiles),
    )
    return analysistest.end(env)

_aspect_refresh_wiring_test = analysistest.make(_aspect_refresh_wiring_test_impl)

def _aspect_excludes_external_test_impl(ctx):
    env = analysistest.begin(ctx)
    target = analysistest.target_under_test(env)
    shards = [
        f.short_path
        for f in target[DefaultInfo].default_runfiles.files.to_list()
        if f.short_path.endswith(".shard.binpb")
    ]
    asserts.true(
        env,
        [s for s in shards if "testdata" in s] != [],
        "the first-party testdata source should be sharded, got: {}".format(shards),
    )
    asserts.equals(
        env,
        [],
        [s for s in shards if "int128" in s or "/absl/" in s],
        "external compile actions must not be sharded (exclude_external_sources), got: {}".format(shards),
    )
    return analysistest.end(env)

_aspect_excludes_external_test = analysistest.make(_aspect_excludes_external_test_impl)

def _aspect_includes_external_test_impl(ctx):
    env = analysistest.begin(ctx)
    target = analysistest.target_under_test(env)
    shards = [
        f.short_path
        for f in target[DefaultInfo].default_runfiles.files.to_list()
        if f.short_path.endswith(".shard.binpb")
    ]

    # Proves the exclusion is real (not vacuous): with the toggle off, the same
    # external int128.cc is sharded.
    asserts.true(
        env,
        [s for s in shards if "int128" in s] != [],
        "with exclude_external_sources = False the external int128.cc should be sharded, got: {}".format(shards),
    )
    return analysistest.end(env)

_aspect_includes_external_test = analysistest.make(_aspect_includes_external_test_impl)

def carve_rules_test_suite(name):
    """Defines the carve_refresh / carve_aspect_refresh analysis tests under `name`.

    Args:
      name: name of the generated `test_suite` that aggregates the rule tests.
    """
    carve_refresh(
        name = "refresh_under_test",
        targets = ["//carve/cdb:cdb_cc"],
        tags = ["manual"],  # Built by the test; not for direct `bazel run`.
    )
    _refresh_wiring_test(name = "refresh_wiring_test", target_under_test = ":refresh_under_test")

    # Layer C: the aspect runs at analysis time over the dep, so the wiring test
    # also proves the aspect declared a shard. The build_test then actually builds
    # the shards, exercising the sandboxed `carve_shard` action in CI.
    carve_aspect_refresh(
        name = "aspect_refresh_under_test",
        targets = ["//rules/testdata:lib"],
        tags = ["manual"],
    )
    _aspect_refresh_wiring_test(name = "aspect_refresh_wiring_test", target_under_test = ":aspect_refresh_under_test")

    # Actually building the shards runs the aspect's `carve_shard` action (exec
    # config) and brings in the launcher's `carve_aggregate` (target config). Both
    # are lean, LLVM-free binaries, so this build_test is cheap and runs in CI: it
    # exercises the whole Layer C build path -- shard production plus the aggregate
    # wiring -- end to end. (It was `manual` while the exec tool was the full
    # LLVM-linked `carve`, which cost ~tens of minutes; carve_shard removed that.)
    build_test(
        name = "aspect_shards_build_test",
        targets = [":aspect_refresh_under_test"],
    )

    # The aspect excludes external compile actions by default: a target depending
    # on a tiny external lib (abseil int128) must shard only its own first-party
    # source, never int128.cc. Analysis-only -- no external code is built.
    carve_aspect_refresh(
        name = "aspect_refresh_external_under_test",
        targets = ["//rules/testdata:lib_with_external_dep"],
        tags = ["manual"],
    )
    _aspect_excludes_external_test(
        name = "aspect_excludes_external_test",
        target_under_test = ":aspect_refresh_external_under_test",
    )
    carve_aspect_refresh(
        name = "aspect_refresh_external_included_under_test",
        targets = ["//rules/testdata:lib_with_external_dep"],
        exclude_external_sources = False,
        tags = ["manual"],
    )
    _aspect_includes_external_test(
        name = "aspect_includes_external_test",
        target_under_test = ":aspect_refresh_external_included_under_test",
    )
    native.test_suite(
        name = name,
        tests = [
            ":refresh_wiring_test",
            ":aspect_refresh_wiring_test",
            ":aspect_shards_build_test",
            ":aspect_excludes_external_test",
            ":aspect_includes_external_test",
        ],
    )
