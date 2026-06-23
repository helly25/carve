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

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "carve/cli/cli.h"
#include "carve/refresh/refresh.h"
#include "carve/scan_deps/scan_deps.h"
#include "mbo/status/status_macros.h"

ABSL_FLAG(
    std::string,
    aquery_proto,
    "",
    "Path to a pre-captured aquery ActionGraphContainer "
    "(from `bazel aquery --output=proto`). Overrides --targets when set.");
ABSL_FLAG(std::string, targets, "//...", "Comma-separated target patterns to aquery when --aquery_proto is not given.");
ABSL_FLAG(std::string, bazel, "bazel", "Path to the bazel binary used to run aquery.");
ABSL_FLAG(std::string, output, "compile_commands.json", "Path of the compilation database to write.");
ABSL_FLAG(std::string, directory, "", "Working directory recorded on each entry; defaults to the current directory.");
ABSL_FLAG(
    std::string,
    sidecar,
    ".carve-cache/entries-by-actionkey.binpb",
    "Action-records sidecar path for incremental refresh; empty disables it.");
ABSL_FLAG(
    std::string,
    project_id,
    "",
    "Project identifier; scopes the sidecar merge so projects sharing a "
    "compilation database do not clobber each other.");
ABSL_FLAG(int, jobs, 0, "Parallel scan-deps worker threads; 0 means use the hardware concurrency.");

namespace {

constexpr std::string_view kUsage =
    "carve <subcommand> [flags]\n"
    "  subcommands: refresh | aggregate | shard | prune\n"
    "  see CARVE_DESIGN.md section 6 for the per-subcommand flag surface";

void PrintError(std::string_view message) {
  std::cerr << "carve: error: " << message << '\n';
}

// Runs the `refresh` subcommand from flags. This is the Layer A path that reads
// a pre-captured aquery proto; in-process aquery and scan-deps land later.
absl::Status RunRefreshFromFlags() {
  std::vector<std::string> targets;
  const std::string targets_flag = absl::GetFlag(FLAGS_targets);
  if (!targets_flag.empty()) {
    targets = absl::StrSplit(targets_flag, ',', absl::SkipEmpty());
  }
  // 0 (the default) means "decide here": use all hardware threads, falling back
  // to serial when the count is unknown.
  int jobs = absl::GetFlag(FLAGS_jobs);
  if (jobs <= 0) {
    jobs = static_cast<int>(std::thread::hardware_concurrency());
  }
  const carve::refresh::FileOptions options{
      .aquery_proto_path = absl::GetFlag(FLAGS_aquery_proto),
      .targets = std::move(targets),
      .bazel_path = absl::GetFlag(FLAGS_bazel),
      .output_path = absl::GetFlag(FLAGS_output),
      .directory = absl::GetFlag(FLAGS_directory),
      .sidecar_path = absl::GetFlag(FLAGS_sidecar),
      .project_id = absl::GetFlag(FLAGS_project_id),
      .scanner = carve::scan_deps::ScanDependencies,
      .clock = [] { return absl::ToUnixSeconds(absl::Now()); },
      .jobs = jobs,
  };
  MBO_ASSIGN_OR_RETURN(const carve::refresh::RefreshStats stats, carve::refresh::RunRefresh(options));
  std::cerr << absl::StreamFormat(
      "carve: wrote %d entries (%d scanned, %d reused)\n", stats.entries, stats.scanned, stats.reused);
  if (stats.unresolved > 0) {
    std::cerr << absl::StreamFormat(
        "carve: warning: %d action(s) have unresolved headers (unbuilt generated headers?); "
        "re-scanned next refresh\n",
        stats.unresolved);
  }
  return absl::OkStatus();
}

int RealMain(int argc, char** argv) {
  absl::SetProgramUsageMessage(kUsage);
  const std::vector<char*> positional = absl::ParseCommandLine(argc, argv);

  // positional[0] is the program name; positional[1], if present, is the
  // subcommand token, and the rest are its arguments.
  if (positional.size() < 2) {
    PrintError("missing subcommand");
    std::cerr << kUsage << '\n';
    return 2;
  }

  const absl::StatusOr<carve::cli::Subcommand> cmd = carve::cli::ParseSubcommand(positional[1]);
  if (!cmd.ok()) {
    PrintError(cmd.status().message());
    return 2;
  }

  std::vector<std::string_view> args;
  args.reserve(positional.size() - 2);
  for (std::size_t i = 2; i < positional.size(); ++i) {
    args.emplace_back(positional[i]);
  }

  // Implemented subcommands are handled here from flags; the rest fall through
  // to the dispatch stub until their modules land.
  absl::Status status;
  switch (*cmd) {
    case carve::cli::Subcommand::kRefresh: status = RunRefreshFromFlags(); break;
    default: status = carve::cli::Dispatch(*cmd, args); break;
  }
  if (!status.ok()) {
    PrintError(status.message());
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  return RealMain(argc, argv);
}
