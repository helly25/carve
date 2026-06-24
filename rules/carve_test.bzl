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
        [p for p in runfiles if p.endswith("carve/carve")] != [],
        "the carve binary should be in the launcher's runfiles, got: {}".format(runfiles),
    )
    asserts.true(
        env,
        [p for p in runfiles if p.endswith(".shard.binpb")] != [],
        "the aspect should have produced at least one shard in the runfiles, got: {}".format(runfiles),
    )
    return analysistest.end(env)

_aspect_refresh_wiring_test = analysistest.make(_aspect_refresh_wiring_test_impl)

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
    # the shards, exercising the sandboxed `carve shard` action (scan-deps) in CI.
    carve_aspect_refresh(
        name = "aspect_refresh_under_test",
        targets = ["//rules/testdata:lib"],
        tags = ["manual"],
    )
    _aspect_refresh_wiring_test(name = "aspect_refresh_wiring_test", target_under_test = ":aspect_refresh_under_test")

    # Actually building a shard runs the aspect's `carve shard` action, which needs
    # the carve binary in the EXEC configuration. Because carve links the
    # from-source LLVM/clang (via scan_deps), that is a full exec-config LLVM build
    # (~tens of minutes) — far too costly for every CI run. So this build_test is
    # `manual` and excluded from the default suite: run it on demand with
    # `bazel test //rules:aspect_shards_build_test`. CI relies on the analysis test
    # above (which validates the wiring without building the exec-config tool); the
    # shard data path itself is covered by //carve/shard:shard_test.
    build_test(
        name = "aspect_shards_build_test",
        targets = [":aspect_refresh_under_test"],
        tags = ["manual"],
    )
    native.test_suite(
        name = name,
        tests = [
            ":refresh_wiring_test",
            ":aspect_refresh_wiring_test",
        ],
    )
