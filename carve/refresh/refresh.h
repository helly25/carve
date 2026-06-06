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

#ifndef CARVE_REFRESH_REFRESH_H_
#define CARVE_REFRESH_REFRESH_H_

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "carve/cdb/cdb.h"

namespace carve::refresh {

// Inputs that shape the produced compilation database.
struct Options {
  // The working directory recorded on each entry (clangd resolves an entry's
  // relative paths against it). For Bazel this is the execution root.
  std::string directory;
};

// File-oriented inputs for `RunRefresh`.
struct FileOptions {
  std::string aquery_proto_path;  // Serialized ActionGraphContainer to read.
  std::string output_path;        // compile_commands.json to (atomically) write.
  std::string directory;          // See Options::directory.
};

// Reads the aquery proto at `options.aquery_proto_path`, builds the entries via
// `BuildEntries`, and atomically writes the compilation database to
// `options.output_path`. Returns a non-OK status if the input cannot be read or
// the output cannot be written.
[[nodiscard]] absl::Status RunRefresh(const FileOptions& options);

// Builds compilation-database entries from serialized aquery
// `ActionGraphContainer` bytes: parse the compile actions, de-Bazel their argv,
// and pair each with the source file detected in that argv. Actions whose
// source cannot be identified are skipped (they cannot form a valid entry).
//
// NOTE: path correctness (execroot vs. workspace, the `//external` symlink
// choreography) is a later quirk; this produces the correct entry *shape*.
[[nodiscard]] absl::StatusOr<std::vector<cdb::CompileCommand>> BuildEntries(
    std::string_view aquery_proto, const Options& options);

}  // namespace carve::refresh

#endif  // CARVE_REFRESH_REFRESH_H_
