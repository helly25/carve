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

#include "carve/shard/shard.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_split.h"
#include "carve/command/command.h"
#include "carve/io/io.h"
#include "carve/sidecar/carve.pb.h"
#include "carve/sidecar/sidecar.h"
#include "mbo/status/status_macros.h"

namespace carve::shard {

ActionRecords BuildShard(const Options& options) {
  std::vector<std::string> argv = command::DeBazel(options.command);
  if (!options.xcode_developer_dir.empty() || !options.xcode_sdkroot.empty()) {
    argv = command::ResolveXcodePlaceholders(argv, options.xcode_developer_dir, options.xcode_sdkroot);
  }

  ActionRecords records;
  ActionRecord& record = *records.add_records();
  record.set_action_key(options.action_key);
  record.add_sources(options.source);
  for (std::string& arg : argv) {
    record.add_command(std::move(arg));
  }
  // Edition 2024 fields have explicit presence, so only set optional fields when
  // they exist; an empty value would serialize and differ from an unset one
  // (matches `refresh::MakeRecord` so the two record shapes are identical).
  if (!options.project_id.empty()) {
    record.set_project_id(options.project_id);
  }
  if (!options.primary_output.empty()) {
    record.set_primary_output(options.primary_output);
  }

  // Scan headers. A failed scan (e.g. an unbuilt generated header) records no
  // headers and leaves the row unstamped, so a later run re-scans it rather than
  // caching an incomplete header set (CARVE_DESIGN.md sections 4.2, 4.4).
  bool scan_complete = true;
  if (options.scanner) {
    const std::vector<std::string> scan_argv(record.command().begin(), record.command().end());
    const absl::StatusOr<std::vector<std::string>> headers = options.scanner(scan_argv, options.directory);
    if (headers.ok()) {
      for (const std::string& header : *headers) {
        // Store execroot-relative (like refresh) so shards are host-independent
        // and remotely cache-shareable (CARVE_DESIGN.md section 9).
        record.add_headers(command::RelativizeToExecroot(header, options.directory));
      }
    } else {
      scan_complete = false;
    }
  }

  // Layer C `ASPECT_M`: record headers parsed from the aspect-scheduled `-M`
  // depfile. The lean carve_shard does not scan; the aspect supplies the
  // dependency set (complete -- the `-M` action either produced the depfile or
  // failed the build), so the row is stamped like a complete scan. Stored
  // execroot-relative so shards stay host-independent (CARVE_DESIGN.md section 9).
  if (!options.dep_headers.empty()) {
    for (const std::string& header : options.dep_headers) {
      record.add_headers(command::RelativizeToExecroot(header, options.directory));
    }
    record.set_source_kind(ActionRecord::ASPECT_M);
  }

  if (options.now && scan_complete) {
    record.set_written_at(options.now());
  }
  return records;
}

absl::Status RunShard(const FileOptions& options) {
  MBO_ASSIGN_OR_RETURN(const std::string contents, io::ReadFile(options.command_file));
  // Multiline param-file format: one argv token per line. Empty lines (a trailing
  // newline, or blank entries) are skipped — compiler args are never empty.
  std::vector<std::string> command;
  for (const std::string_view token : absl::StrSplit(contents, '\n', absl::SkipEmpty())) {
    command.emplace_back(token);
  }

  // ASPECT_M: parse the aspect-scheduled `-M` depfile into the header set.
  std::vector<std::string> dep_headers;
  if (!options.depfile.empty()) {
    MBO_ASSIGN_OR_RETURN(const std::string depfile_contents, io::ReadFile(options.depfile));
    dep_headers = command::ParseMakeDependencies(depfile_contents);
  }

  const ActionRecords shard = BuildShard(
      Options{
          .action_key = options.action_key,
          .command = std::move(command),
          .source = options.source,
          .project_id = options.project_id,
          .primary_output = options.primary_output,
          .directory = options.directory,
          .xcode_developer_dir = options.xcode_developer_dir,
          .xcode_sdkroot = options.xcode_sdkroot,
          .scanner = options.scanner,
          .dep_headers = std::move(dep_headers),
          .now = options.now,
      });
  return sidecar::Save(options.out_path, shard);
}

}  // namespace carve::shard
