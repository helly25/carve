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

#ifndef CARVE_SCAN_DEPS_SCAN_DEPS_H_
#define CARVE_SCAN_DEPS_SCAN_DEPS_H_

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace carve::scan_deps {

// Scans the dependencies of a single compile command using clang's in-process
// `DependencyScanningTool`. `args` is the full driver command line (compiler +
// flags + source); `working_dir` resolves the command's relative paths. Returns
// the dependency paths the scanner reports - the translation unit's source plus
// the headers it includes. Returns a non-OK status if scanning fails (e.g. a
// missing header). The scan is performed entirely in-process (no subprocess).
[[nodiscard]] absl::StatusOr<std::vector<std::string>> ScanDependencies(
    absl::Span<const std::string> args,
    std::string_view working_dir);

}  // namespace carve::scan_deps

#endif  // CARVE_SCAN_DEPS_SCAN_DEPS_H_
