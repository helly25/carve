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

  // Identifies the refreshing project; records are stamped with it and the
  // sidecar merge is scoped to it so projects sharing a CDB do not clobber one
  // another. Empty means the unnamed default project.
  std::string project_id;
};

// File-oriented inputs for `RunRefresh`.
struct FileOptions {
  std::string aquery_proto_path;     // Pre-captured ActionGraphContainer; takes
                                     // precedence over running aquery.
  std::vector<std::string> targets;  // Target patterns to aquery when no proto
                                     // path is given (e.g. {"//..."}).
  std::string bazel_path;            // bazel binary; empty means "bazel" on PATH.
  std::string output_path;           // compile_commands.json to (atomically) write.
  std::string directory;             // Entry directory; empty => `bazel info execution_root`.
  std::string sidecar_path;          // Action-records sidecar; empty disables it.
  std::string project_id;            // See Options::project_id.
};

// Obtains the aquery proto — by reading `aquery_proto_path` if set, otherwise by
// running `bazel aquery` over `targets` — builds the current action records,
// and (when `sidecar_path` is set) merges them against the stored sidecar
// (reusing cached records for unchanged actions, scoped to `project_id`), writes
// the sidecar back, and emits the compilation database from the merged set.
// Returns a non-OK status if no input source is given, the aquery fails, or any
// file cannot be read or written.
[[nodiscard]] absl::Status RunRefresh(const FileOptions& options);

// Builds compilation-database entries from serialized aquery
// `ActionGraphContainer` bytes: parse the compile actions, de-Bazel their argv,
// and pair each with the source file detected in that argv. The `file` is made
// absolute against `directory` (the execroot) so clangd matches it
// unambiguously. Actions whose source cannot be identified are skipped (they
// cannot form a valid entry).
//
// NOTE: the remaining path quirks (execroot-vs-workspace source mapping, the
// `//external` symlink choreography, compiler-wrapper resolution) are still to
// come; this produces the correct entry shape with absolute source paths.
[[nodiscard]] absl::StatusOr<std::vector<cdb::CompileCommand>> BuildEntries(
    std::string_view aquery_proto,
    const Options& options);

}  // namespace carve::refresh

#endif  // CARVE_REFRESH_REFRESH_H_
