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

// `carve_aggregate` is the lean build of the `aggregate` subcommand: it merges
// independently-produced shards/sidecars into one compilation database and links
// neither scan_deps nor the from-source LLVM (merging proto records needs no
// compiler). The Layer C launcher (`carve_aspect_refresh`) runs it once to fold
// the per-action shards into `compile_commands.json`, so `bazel run` of a Layer C
// refresh - and the aspect's `build_test` - never trigger a full LLVM build.
// Standalone aggregation also stays in the full `carve aggregate` binary.
//
// The flag surface mirrors `carve aggregate` (the CLI glue is duplicated so this
// binary carries none of the full binary's dependencies).

#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "carve/aggregate/aggregate.h"

ABSL_FLAG(std::string, sidecars, "", "Comma-separated sidecar (shard) paths to merge into one database.");
ABSL_FLAG(std::string, output, "compile_commands.json", "Path to write the merged compilation database (JSON).");
ABSL_FLAG(std::string, directory, "", "Shared execroot the sidecars' sources resolve against; defaults to the cwd.");
// Parsed for parity with `carve aggregate` and the launcher's invocation; the
// merge unions every sidecar, so it does not currently scope by project.
ABSL_FLAG(std::string, project_id, "", "Project identifier (accepted for CLI parity; unused by the merge).");

namespace {

int RealMain(int argc, char** argv) {
  absl::SetProgramUsageMessage("carve_aggregate --sidecars=A,B,C --output=compile_commands.json [flags]");
  absl::ParseCommandLine(argc, argv);

  std::vector<std::filesystem::path> sidecars;
  for (const std::string_view path : absl::StrSplit(absl::GetFlag(FLAGS_sidecars), ',', absl::SkipEmpty())) {
    sidecars.emplace_back(path);
  }
  if (sidecars.empty()) {
    std::cerr << "carve_aggregate: error: --sidecars must list at least one sidecar path\n";
    return 2;
  }

  std::string directory = absl::GetFlag(FLAGS_directory);
  if (directory.empty()) {
    directory = std::filesystem::current_path().string();
  }

  const absl::StatusOr<int> entries = carve::aggregate::RunAggregate(sidecars, absl::GetFlag(FLAGS_output), directory);
  if (!entries.ok()) {
    std::cerr << "carve_aggregate: error: " << entries.status().message() << '\n';
    return 1;
  }
  std::cerr << absl::StreamFormat(
      "carve_aggregate: wrote %d entries from %d sidecar(s)\n", *entries, static_cast<int>(sidecars.size()));
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  return RealMain(argc, argv);
}
