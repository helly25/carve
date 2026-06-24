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

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "carve/cdb/cdb.h"
#include "carve/sidecar/carve.pb.h"

namespace carve::refresh {

// Scans a compile command's header dependencies: given the (de-Bazeled) argv and
// the working directory, returns the dependency paths. Injected so `refresh`
// stays free of a hard libclang/platform dependency and is unit-testable with a
// fake; the production scanner is `scan_deps::ScanDependencies`. An empty
// scanner leaves records' `headers` unset.
//
// MUST be safe to call concurrently from multiple threads: `refresh` scans
// actions in parallel (see `FileOptions::jobs`). `scan_deps::ScanDependencies`
// qualifies — it builds its own dependency-scanning service per call.
using HeaderScanner = std::function<
    absl::StatusOr<std::vector<std::string>>(absl::Span<const std::string> argv, std::string_view directory)>;

// Returns the current time in unix seconds. Injected so `refresh` stamps
// `written_at` deterministically in tests; the production clock reads the wall
// clock. An empty clock leaves `written_at` unset.
using Clock = std::function<std::int64_t()>;

// Resolved Apple toolchain paths used to substitute Bazel `wrapped_clang`
// placeholders (`__BAZEL_XCODE_DEVELOPER_DIR__` / `__BAZEL_XCODE_SDKROOT__`) that
// appear in compile commands from the Apple crosstool. An empty field leaves its
// placeholder untouched.
struct XcodePaths {
  std::string developer_dir;
  std::string sdkroot;
};

// Resolves the Apple toolchain paths (e.g. `xcode-select -p` / `xcrun
// --show-sdk-path` on macOS). Injected so `refresh` stays IO-free and testable;
// only called when a command actually carries a `__BAZEL_XCODE_` placeholder.
// An empty resolver disables the substitution.
using XcodeResolver = std::function<XcodePaths()>;

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
  HeaderScanner scanner;             // Scans added/changed actions; empty = no header scanning.
  Clock clock;                       // Stamps written_at on this project's records each
                                     // refresh (for prune/GC); empty leaves it unset.
  int jobs = 0;                      // Parallel scan-deps worker threads; <=0 means serial.
  XcodeResolver xcode_resolver;      // Resolves wrapped_clang placeholders; empty = none.
};

// Outcome counts from a refresh, for reporting to the user.
struct RefreshStats {
  int entries = 0;     // Entries written to the compilation database (all projects).
  int scanned = 0;     // Actions (re)scanned this run, including failures.
  int reused = 0;      // Own-project actions whose cached headers were reused.
  int unresolved = 0;  // Actions whose scan failed (e.g. unbuilt generated headers);
                       // left unstamped so the next refresh re-scans them.
};

// Obtains the aquery proto — by reading `aquery_proto_path` if set, otherwise by
// running `bazel aquery` over `targets` — builds the current action records,
// and (when `sidecar_path` is set) merges them against the stored sidecar
// (reusing cached records for unchanged actions, scoped to `project_id`), writes
// the sidecar back, and emits the compilation database from the merged set.
// Returns the refresh `RefreshStats` on success, or a non-OK status if no input
// source is given, the aquery fails, or any file cannot be read or written.
[[nodiscard]] absl::StatusOr<RefreshStats> RunRefresh(const FileOptions& options);

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

// Projects action `records` into compilation-database entries: each record with
// a source becomes one entry whose `file` is made absolute against `directory`
// (the execroot). Records without a source are skipped. Shared by `refresh` and
// `aggregate` (which merges independently-produced sidecars into one database).
[[nodiscard]] std::vector<cdb::CompileCommand> EntriesFromRecords(
    const ActionRecords& records,
    std::string_view directory);

}  // namespace carve::refresh

#endif  // CARVE_REFRESH_REFRESH_H_
