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
#
# BUILD overlay for a prebuilt LLVM/Clang release. Exposes the clang C++ shared
# library (which statically embeds LLVM) and its headers for in-process
# dependency scanning.

load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "dependency_scanning",
    srcs = ["lib/libclang-cpp.dylib"],
    hdrs = glob([
        "include/clang/**",
        "include/clang-c/**",
        "include/llvm/**",
        "include/llvm-c/**",
    ]),
    # LLVM's headers are not standalone-parseable and form one big layer.
    features = [
        "-layering_check",
        "-parse_headers",
    ],
    # `includes` adds the path as `-isystem`, so LLVM's own headers are treated
    # as system headers and their warnings do not trip our `-Werror`.
    includes = ["include"],
    linkopts = ["-lz"],  # libclang-cpp's only non-system runtime dep (libc++ via the toolchain).
    target_compatible_with = [
        "@platforms//os:macos",
        "@platforms//cpu:arm64",
    ],
)
