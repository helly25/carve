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
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "carve/cli/cli.h"
#include "carve/refresh/refresh.h"

ABSL_FLAG(std::string, aquery_proto, "",
          "Path to a serialized aquery ActionGraphContainer "
          "(from `bazel aquery --output=proto`). Required by `refresh`.");
ABSL_FLAG(std::string, output, "compile_commands.json",
          "Path of the compilation database to write.");
ABSL_FLAG(std::string, directory, "",
          "Working directory recorded on each entry; defaults to the current directory.");
ABSL_FLAG(std::string, sidecar, ".carve-cache/entries-by-actionkey.binpb",
          "Action-records sidecar path for incremental refresh; empty disables it.");
ABSL_FLAG(std::string, project_id, "",
          "Project identifier; scopes the sidecar merge so projects sharing a "
          "compilation database do not clobber each other.");

namespace {

constexpr std::string_view kUsage =
    "carve <subcommand> [flags]\n"
    "  subcommands: refresh | aggregate | shard | prune\n"
    "  see CARVE_DESIGN.md section 6 for the per-subcommand flag surface";

void PrintError(std::string_view message) {
  std::fputs(absl::StrCat("carve: error: ", message, "\n").c_str(), stderr);
}

// Runs the `refresh` subcommand from flags. This is the Layer A path that reads
// a pre-captured aquery proto; in-process aquery and scan-deps land later.
absl::Status RunRefreshFromFlags() {
  const std::string aquery_proto = absl::GetFlag(FLAGS_aquery_proto);
  if (aquery_proto.empty()) {
    return absl::InvalidArgumentError("refresh requires --aquery_proto=PATH");
  }
  std::string directory = absl::GetFlag(FLAGS_directory);
  if (directory.empty()) {
    directory = std::filesystem::current_path().string();
  }
  return carve::refresh::RunRefresh(carve::refresh::FileOptions{
      .aquery_proto_path = aquery_proto,
      .output_path = absl::GetFlag(FLAGS_output),
      .directory = directory,
      .sidecar_path = absl::GetFlag(FLAGS_sidecar),
      .project_id = absl::GetFlag(FLAGS_project_id),
  });
}

int RealMain(int argc, char** argv) {
  absl::SetProgramUsageMessage(kUsage);
  const std::vector<char*> positional = absl::ParseCommandLine(argc, argv);

  // positional[0] is the program name; positional[1], if present, is the
  // subcommand token, and the rest are its arguments.
  if (positional.size() < 2) {
    PrintError("missing subcommand");
    std::fputs(absl::StrCat(kUsage, "\n").c_str(), stderr);
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
    case carve::cli::Subcommand::kRefresh:
      status = RunRefreshFromFlags();
      break;
    default:
      status = carve::cli::Dispatch(*cmd, args);
      break;
  }
  if (!status.ok()) {
    PrintError(status.message());
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) { return RealMain(argc, argv); }
