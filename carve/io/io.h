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

#ifndef CARVE_IO_IO_H_
#define CARVE_IO_IO_H_

#include <filesystem>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

// Small filesystem helpers shared across modules. These are general-purpose and
// are candidates to migrate to mbo once it grows an atomic write and a
// view-returning reader (CARVE_DESIGN.md section 10); they live here until then.
namespace carve::io {

// Atomically replaces the file at `path` with `content` by writing to a sibling
// temporary file and renaming it into place (rename is atomic within a
// filesystem). Parent directories are created if missing. On failure `path` is
// left untouched.
[[nodiscard]] absl::Status WriteAtomically(const std::filesystem::path& path,
                                           std::string_view content);

// Reads the entire file at `path`. Returns `NotFoundError` if it does not
// exist, or another non-OK status if it cannot be read.
[[nodiscard]] absl::StatusOr<std::string> ReadFile(const std::filesystem::path& path);

}  // namespace carve::io

#endif  // CARVE_IO_IO_H_
