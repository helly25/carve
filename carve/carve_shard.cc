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

// `carve_shard` is the lean, scan-free build of the `shard` subcommand: it does
// NOT link scan_deps / the from-source LLVM, so it builds in seconds instead of a
// full LLVM compile. The Layer C aspect (`cc_carve_aspect`) runs it once per
// compile action as a build tool, where a per-action LLVM-linked exec tool would
// be untenable. It injects no scanner; by default it records command + source
// only and Bazel owns per-action invalidation. When the aspect runs with header
// recording on, it passes the compiler's `-M` depfile via `--depfile`, and
// carve_shard records those headers (ASPECT_M) by parsing the make-format file --
// still no LLVM. Standalone scanning stays in the full `carve shard` binary.
//
// The flag surface mirrors `carve shard`; the CLI glue is intentionally
// duplicated here so this binary carries none of the full binary's dependencies.

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "carve/process/process.h"
#include "carve/shard/shard.h"

ABSL_FLAG(std::string, action_key, "", "Identity key of the compile action this shard describes.");
ABSL_FLAG(
    std::string,
    command_file,
    "",
    "Path to the compiler argv, one token per line (Bazel multiline param format).");
ABSL_FLAG(std::string, source, "", "Exec-root-relative source path of the compile action.");
ABSL_FLAG(std::string, primary_output, "", "Exec-root-relative primary output of the compile action (optional).");
ABSL_FLAG(std::string, out, "", "Path to write the resulting one-record shard (binary proto).");
ABSL_FLAG(std::string, project_id, "", "Project identifier stamped on the record.");
ABSL_FLAG(std::string, directory, "", "Execroot the command's relative paths resolve against; defaults to the cwd.");
ABSL_FLAG(
    std::string,
    xcode_developer_dir,
    "",
    "Value for __BAZEL_XCODE_DEVELOPER_DIR__; resolved via xcode-select on macOS when empty.");
ABSL_FLAG(std::string, xcode_sdkroot, "", "Value for __BAZEL_XCODE_SDKROOT__; resolved via xcrun on macOS when empty.");
ABSL_FLAG(
    std::string,
    depfile,
    "",
    "Optional make-format `-M` depfile the aspect scheduled; its headers are parsed and recorded as ASPECT_M.");

namespace {

// Trims trailing whitespace from a captured command's stdout.
std::string TrimmedOutput(const std::vector<std::string>& argv) {
#if defined(__APPLE__)
  const absl::StatusOr<carve::process::CommandResult> result = carve::process::Run(argv);
  if (!result.ok() || result->exit_code != 0) {
    return "";
  }
  std::string out = result->stdout_data;
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) {
    out.pop_back();
  }
  return out;
#else
  (void)argv;
  return "";
#endif
}

int RealMain(int argc, char** argv) {
  absl::SetProgramUsageMessage("carve_shard --action_key=K --command_file=F --source=S --out=O [flags]");
  absl::ParseCommandLine(argc, argv);

  const std::string action_key = absl::GetFlag(FLAGS_action_key);
  const std::string command_file = absl::GetFlag(FLAGS_command_file);
  const std::string source = absl::GetFlag(FLAGS_source);
  const std::string out = absl::GetFlag(FLAGS_out);
  if (action_key.empty() || command_file.empty() || source.empty() || out.empty()) {
    std::cerr << "carve_shard: error: --action_key, --command_file, --source and --out are required\n";
    return 2;
  }

  std::string directory = absl::GetFlag(FLAGS_directory);
  if (directory.empty()) {
    directory = std::filesystem::current_path().string();
  }

  // Resolve the Apple toolchain paths (macOS) unless the caller supplied them.
  std::string xcode_developer_dir = absl::GetFlag(FLAGS_xcode_developer_dir);
  std::string xcode_sdkroot = absl::GetFlag(FLAGS_xcode_sdkroot);
  if (xcode_developer_dir.empty() && xcode_sdkroot.empty()) {
    xcode_developer_dir = TrimmedOutput({"xcode-select", "-p"});
    xcode_sdkroot = TrimmedOutput({"xcrun", "--sdk", "macosx", "--show-sdk-path"});
  }

  const carve::shard::FileOptions options{
      .action_key = action_key,
      .command_file = command_file,
      .source = source,
      .project_id = absl::GetFlag(FLAGS_project_id),
      .primary_output = absl::GetFlag(FLAGS_primary_output),
      .directory = directory,
      .xcode_developer_dir = xcode_developer_dir,
      .xcode_sdkroot = xcode_sdkroot,
      // No scanner: this binary links no scan-deps. Headers, when requested, come
      // from the aspect's `-M` depfile (below), parsed without the scanner.
      .scanner = {},
      .depfile = absl::GetFlag(FLAGS_depfile),
      // Stamp written_at so aggregate's newest-wins dedup behaves as for `carve shard`.
      .now = [] { return absl::ToUnixSeconds(absl::Now()); },
      .out_path = out,
  };
  const absl::Status status = carve::shard::RunShard(options);
  if (!status.ok()) {
    std::cerr << "carve_shard: error: " << status.message() << '\n';
    return 1;
  }
  std::cerr << absl::StreamFormat("carve_shard: wrote shard %s\n", out);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  return RealMain(argc, argv);
}
