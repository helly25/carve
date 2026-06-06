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

#ifndef CARVE_CDB_CDB_H_
#define CARVE_CDB_CDB_H_

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/types/span.h"

namespace carve::cdb {

// A single entry of a JSON Compilation Database (compile_commands.json). See
// https://clang.llvm.org/docs/JSONCompilationDatabase.html. `file` and
// `directory` are required; `arguments` carries the argv form clangd prefers.
// An empty `output` is omitted from the serialized entry.
struct CompileCommand {
  std::string directory;
  std::string file;
  std::vector<std::string> arguments;
  std::string output;
};

// Serializes `entries` as a JSON compilation database: a JSON array with stable
// two-space indentation and a trailing newline. The output is deterministic for
// a given input (no map iteration, no timestamps), so it is safe to diff and to
// compare byte-for-byte across runs.
[[nodiscard]] std::string ToJson(absl::Span<const CompileCommand> entries);

// Atomically replaces the file at `path` with `content` by writing to a sibling
// temporary file and renaming it into place (rename is atomic within a
// filesystem). Parent directories are created if missing. Returns a non-OK
// status if any step fails; on failure `path` is left untouched.
[[nodiscard]] absl::Status WriteAtomically(const std::filesystem::path& path, std::string_view content);

// Convenience: `ToJson` followed by `WriteAtomically`.
[[nodiscard]] absl::Status Write(const std::filesystem::path& path, absl::Span<const CompileCommand> entries);

}  // namespace carve::cdb

#endif  // CARVE_CDB_CDB_H_
