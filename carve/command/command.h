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
#include <vector>

#include "absl/types/span.h"

namespace carve::command {

// Applies the IO-free, platform-agnostic de-Bazeling argv transforms so clangd
// can consume a Bazel compile command. Currently drops flags clangd cannot use:
//
//   * `-fno-canonical-system-headers` (clangd#1004)
//   * `-gcc-toolchain` / `--gcc-toolchain` and their `=`-joined form
//     (clangd#1248)
//
// Argument order is otherwise preserved. This is a pure function: no filesystem
// or environment access. Quirks that need the execroot, response-file contents,
// or platform-specific wrappers live elsewhere. See CARVE_DESIGN.md section 4.3.
[[nodiscard]] std::vector<std::string> DeBazel(absl::Span<const std::string> argv);

}  // namespace carve::command

#endif  // CARVE_COMMAND_COMMAND_H_
