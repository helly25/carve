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

#ifndef CARVE_PROCESS_PROCESS_H_
#define CARVE_PROCESS_PROCESS_H_

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"

// Subprocess execution. This is a general-purpose helper and is a candidate to
// migrate to mbo (CARVE_DESIGN.md sections 4.1 and 10); it lives here until mbo
// grows a subprocess wrapper. POSIX-only for now; Windows is a later concern.
namespace carve::process {

// The outcome of running a subprocess to completion.
struct CommandResult {
  int exit_code = 0;        // 127 if the program was not found; 128+signal if killed.
  std::string stdout_data;  // Captured standard output (binary-safe).
  std::string stderr_data;  // Captured standard error.
};

// Runs `argv` to completion (argv[0] is resolved on PATH), capturing stdout and
// stderr separately and concurrently (so large output does not deadlock).
// Returns a non-OK status only if the child cannot be spawned or waited on; a
// non-zero exit is reported via `CommandResult::exit_code`, not as an error.
[[nodiscard]] absl::StatusOr<CommandResult> Run(absl::Span<const std::string> argv);

}  // namespace carve::process

#endif  // CARVE_PROCESS_PROCESS_H_
