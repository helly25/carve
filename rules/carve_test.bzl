# SPDX-FileCopyrightText: Copyright (c) The helly25/carve authors (github.com/helly25/carve)
# SPDX-License-Identifier: Apache-2.0
"""Analysis tests for `carve_refresh` (rules/carve.bzl).

These run in `bazel test` without executing the target, so they validate the
rule's wiring (it is runnable and carries the carve binary in its runfiles)
without the nested-bazel trap that running `carve refresh` inside a test would
hit. The end-to-end behavior is dogfooded via `bazel run //:refresh`.
"""

load("@bazel_skylib//lib:unittest.bzl", "analysistest", "asserts")
load(":carve.bzl", "carve_refresh")

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

def carve_rules_test_suite(name):
    """Defines the carve_refresh analysis tests under `name`."""
    carve_refresh(
        name = "refresh_under_test",
        targets = ["//carve/cdb:cdb_cc"],
        tags = ["manual"],  # Built by the test; not for direct `bazel run`.
    )
    _refresh_wiring_test(name = "refresh_wiring_test", target_under_test = ":refresh_under_test")
    native.test_suite(name = name, tests = [":refresh_wiring_test"])
