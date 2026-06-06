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

#ifndef CARVE_AQUERY_AQUERY_H_
#define CARVE_AQUERY_AQUERY_H_

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace carve::aquery {

// A C/C++/Objective-C compile action extracted from an aquery action graph.
struct CompileAction {
  std::string action_key;
  std::string mnemonic;
  std::vector<std::string> arguments;
  std::string primary_output;  // Exec-root-relative path of the primary output.
};

// Parses serialized `analysis.ActionGraphContainer` bytes (as produced by
// `bazel aquery --output=proto`) and returns the compile actions it contains
// (mnemonics CppCompile / ObjcCompile / CppModuleCompile). The primary output
// path is resolved from the action graph's path-fragment table, and response
// files (`@path`) are expanded inline from the action's embedded param files
// when present (`bazel aquery --include_param_files`). Returns
// `InvalidArgumentError` if the bytes are not a valid ActionGraphContainer.
[[nodiscard]] absl::StatusOr<std::vector<CompileAction>> ParseCompileActions(
    std::string_view serialized);

}  // namespace carve::aquery

#endif  // CARVE_AQUERY_AQUERY_H_
