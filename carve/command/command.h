// SPDX-FileCopyrightText: Copyright (c) The helly25/carve authors (github.com/helly25/carve)
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef CARVE_COMMAND_COMMAND_H_
#define CARVE_COMMAND_COMMAND_H_

#include <string>
#include <string_view>
#include <vector>

#include "absl/types/span.h"

namespace carve::command {

// Applies the IO-free, platform-agnostic de-Bazeling argv transforms so clangd
// can consume a Bazel compile command. It:
//
//   * drops a leading `ccache` compiler wrapper, so argv[0] is the real
//     compiler clangd can introspect (ccache docs);
//   * drops flags clangd cannot use or does not need:
//       - `-fno-canonical-system-headers` (clangd#1004),
//       - `-gcc-toolchain` / `--gcc-toolchain` and their `=`-joined form
//         (clangd#1248),
//       - `/showIncludes`[`:user`] (MSVC dependency listing; Ninja issue 613),
//       - `-fmodules-cache-path=bazel-out/...` (a per-build modules cache dir).
//
// Argument order is otherwise preserved. This is a pure function: no filesystem
// or environment access (the leading-`ccache` check is a lexical basename test).
// Quirks that need the execroot, response-file contents, or resolved toolchain
// paths live elsewhere (see `ResolveXcodePlaceholders` and the refresh layer).
// See CARVE_DESIGN.md section 4.3.
[[nodiscard]] std::vector<std::string> DeBazel(absl::Span<const std::string> argv);

// Substitutes Bazel's Apple `wrapped_clang` placeholders in `argv`:
// `__BAZEL_XCODE_DEVELOPER_DIR__` -> `developer_dir` and
// `__BAZEL_XCODE_SDKROOT__` -> `sdkroot` (as substrings, e.g. inside
// `-isysroot__BAZEL_XCODE_SDKROOT__`). Bazel emits these unresolved in the
// action proto and resolves them from the environment at exec time, but clangd
// needs the real paths. An empty replacement leaves its placeholder untouched.
// Pure; the caller resolves the paths (macOS `xcode-select`/`xcrun`) and passes
// them in. See Bazel tools/osx/crosstool/wrapped_clang.cc.
[[nodiscard]] std::vector<std::string> ResolveXcodePlaceholders(
    absl::Span<const std::string> argv,
    std::string_view developer_dir,
    std::string_view sdkroot);

}  // namespace carve::command

#endif  // CARVE_COMMAND_COMMAND_H_
