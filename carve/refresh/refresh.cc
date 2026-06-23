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

#include "carve/refresh/refresh.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "carve/aquery/aquery.h"
#include "carve/cdb/cdb.h"
#include "carve/command/command.h"
#include "carve/io/io.h"
#include "carve/process/process.h"
#include "carve/sidecar/carve.pb.h"
#include "carve/sidecar/sidecar.h"

namespace carve::refresh {
namespace {

// True if `arg` names a C/C++/Objective-C/CUDA translation unit by extension.
bool IsSourceFile(std::string_view arg) {
  const std::string_view::size_type dot = arg.rfind('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  const std::string_view ext = arg.substr(dot + 1);
  static const absl::flat_hash_set<std::string_view>* const kSourceExt =
      new absl::flat_hash_set<std::string_view>{"c", "cc", "cpp", "cxx", "c++", "cu", "m", "mm"};
  return kSourceExt->contains(ext);
}

// Returns the source operand of a compile command: the last argv token that
// looks like a source file. Empty if none is present.
std::string_view FindSource(const std::vector<std::string>& argv) {
  std::string_view source;
  for (const std::string& arg : argv) {
    if (IsSourceFile(arg)) {
      source = arg;
    }
  }
  return source;
}

// Resolves a (possibly exec-root-relative) source path to an absolute path so
// clangd matches it unambiguously. A path that is already absolute, or any path
// when `directory` is empty, is returned unchanged.
std::string AbsoluteFile(std::string_view directory, std::string_view source) {
  const std::filesystem::path path(source);
  if (directory.empty() || path.is_absolute()) {
    return std::string(source);
  }
  return (std::filesystem::path(directory) / path).lexically_normal().string();
}

// Maps one compile action to an ActionRecord, de-Bazeling its argv and detecting
// the source operand. Headers are NOT scanned here; `RunRefresh` scans only the
// added/changed records (see `ScanHeaders`). Returns nullopt when no source can
// be identified (such an action cannot form a valid compilation-database entry).
std::optional<ActionRecord> MakeRecord(const aquery::CompileAction& action, std::string_view project_id) {
  std::vector<std::string> arguments = command::DeBazel(action.arguments);
  const std::string_view source = FindSource(arguments);
  if (source.empty()) {
    return std::nullopt;
  }
  ActionRecord record;
  record.set_action_key(action.action_key);
  record.add_sources(std::string(source));  // Copies before `arguments` is moved.
  for (std::string& arg : arguments) {
    record.add_command(std::move(arg));
  }
  // Edition 2024 fields have explicit presence, so only set optional fields when
  // they exist; otherwise an empty value would serialize and differ from a
  // record that never had one.
  if (!action.primary_output.empty()) {
    record.set_primary_output(action.primary_output);
  }
  if (!project_id.empty()) {
    record.set_project_id(project_id);
  }
  return record;
}

// Builds the current action records (all stamped with `project_id`, no headers
// yet) from serialized aquery bytes.
absl::StatusOr<ActionRecords> BuildRecords(std::string_view aquery_proto, std::string_view project_id) {
  absl::StatusOr<std::vector<aquery::CompileAction>> actions = aquery::ParseCompileActions(aquery_proto);
  if (!actions.ok()) {
    return actions.status();
  }
  ActionRecords records;
  for (const aquery::CompileAction& action : *actions) {
    std::optional<ActionRecord> record = MakeRecord(action, project_id);
    if (record.has_value()) {
      *records.add_records() = *std::move(record);
    }
  }
  return records;
}

// Scans `record`'s command for header dependencies (against `directory`) and
// stores them on the record. A scan failure leaves `headers` unset (the action
// is simply not header-cached this run).
void ScanHeaders(ActionRecord& record, std::string_view directory, const HeaderScanner& scanner) {
  std::vector<std::string> argv;
  argv.reserve(static_cast<std::size_t>(record.command_size()));
  for (int i = 0; i < record.command_size(); ++i) {
    argv.emplace_back(record.command(i));
  }
  absl::StatusOr<std::vector<std::string>> headers = scanner(argv, directory);
  if (headers.ok()) {
    for (const std::string& header : *headers) {
      record.add_headers(header);
    }
  }
}

// True if `path` exists and was last modified strictly after `since` (unix
// seconds). A path that cannot be stat'd (missing, unreadable) returns false: we
// cannot prove it changed, so it does not on its own force a re-scan.
bool ModifiedAfter(const std::filesystem::path& path, std::int64_t since) {
  std::error_code error;
  const std::filesystem::file_time_type mtime = std::filesystem::last_write_time(path, error);
  if (error) {
    return false;
  }
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::file_clock::to_sys(mtime).time_since_epoch())
          .count();
  return seconds > since;
}

// True if `stored`'s cached scan can no longer be trusted: any source or header
// it recorded has been modified since `written_at`, so its include set may have
// changed and the action must be re-scanned. An unstamped record (written_at 0)
// has no baseline and is always treated as stale. Sources are resolved against
// `directory` (the execroot); scan-deps headers are already absolute.
//
// `written_at` has one-second granularity, so an edit made in the same second as
// the previous refresh's stamp is not detected until the next edit or refresh —
// acceptable for a CDB cache, and clangd re-preprocesses regardless.
bool CachedScanIsStale(const ActionRecord& stored, std::string_view directory) {
  if (stored.written_at() == 0) {
    return true;
  }
  for (int i = 0; i < stored.sources_size(); ++i) {
    if (ModifiedAfter(AbsoluteFile(directory, stored.sources(i)), stored.written_at())) {
      return true;
    }
  }
  for (int i = 0; i < stored.headers_size(); ++i) {
    if (ModifiedAfter(std::filesystem::path(std::string(stored.headers(i))), stored.written_at())) {
      return true;
    }
  }
  return false;
}

// Projects action records into compilation-database entries.
std::vector<cdb::CompileCommand> EntriesFromRecords(const ActionRecords& records, std::string_view directory) {
  std::vector<cdb::CompileCommand> entries;
  entries.reserve(static_cast<std::size_t>(records.records_size()));
  for (const ActionRecord& record : records.records()) {
    if (record.sources_size() == 0) {
      continue;
    }
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(record.command_size()));
    for (int i = 0; i < record.command_size(); ++i) {
      arguments.emplace_back(record.command(i));
    }
    entries.push_back(
        cdb::CompileCommand{
            .directory = std::string(directory),
            .file = AbsoluteFile(directory, record.sources(0)),
            .arguments = std::move(arguments),
            .output = std::string(record.primary_output()),
        });
  }
  return entries;
}

// Returns `bazel info execution_root` (trimmed): the directory clangd should
// resolve each entry's relative paths against.
absl::StatusOr<std::string> BazelExecRoot(const std::string& bazel) {
  absl::StatusOr<process::CommandResult> result =
      process::Run({bazel.empty() ? "bazel" : bazel, "info", "execution_root"});
  if (!result.ok()) {
    return result.status();
  }
  if (result->exit_code != 0) {
    return absl::UnknownError(
        absl::StrCat("bazel info execution_root failed (exit ", result->exit_code, "): ", result->stderr_data));
  }
  std::string root = std::move(result->stdout_data);
  while (!root.empty() && (root.back() == '\n' || root.back() == '\r' || root.back() == ' ')) {
    root.pop_back();
  }
  return root;
}

// Runs `bazel aquery --output=proto` over `targets`, filtered to compile
// mnemonics and with param files embedded, and returns the serialized
// ActionGraphContainer bytes.
absl::StatusOr<std::string> RunAquery(const std::vector<std::string>& targets, const std::string& bazel) {
  const std::string expr =
      absl::StrCat("mnemonic(\"CppCompile|ObjcCompile|CppModuleCompile\", ", absl::StrJoin(targets, " + "), ")");
  const std::vector<std::string> argv = {
      bazel.empty() ? "bazel" : bazel, "aquery", "--output=proto", "--include_param_files", expr};
  absl::StatusOr<process::CommandResult> result = process::Run(argv);
  if (!result.ok()) {
    return result.status();
  }
  if (result->exit_code != 0) {
    return absl::UnknownError(
        absl::StrCat("bazel aquery failed (exit ", result->exit_code, "): ", result->stderr_data));
  }
  return std::move(result->stdout_data);
}

}  // namespace

absl::StatusOr<std::vector<cdb::CompileCommand>> BuildEntries(std::string_view aquery_proto, const Options& options) {
  const absl::StatusOr<ActionRecords> records = BuildRecords(aquery_proto, options.project_id);
  if (!records.ok()) {
    return records.status();
  }
  return EntriesFromRecords(*records, options.directory);
}

absl::Status RunRefresh(const FileOptions& options) {
  absl::StatusOr<std::string> proto;
  if (!options.aquery_proto_path.empty()) {
    proto = io::ReadFile(options.aquery_proto_path);
  } else if (!options.targets.empty()) {
    proto = RunAquery(options.targets, options.bazel_path);
  } else {
    return absl::InvalidArgumentError("refresh needs --aquery_proto or --targets");
  }
  if (!proto.ok()) {
    return proto.status();
  }

  // Default the entry directory to the execroot so exec-relative argv resolve.
  std::string directory = options.directory;
  if (directory.empty()) {
    absl::StatusOr<std::string> execroot = BazelExecRoot(options.bazel_path);
    if (!execroot.ok()) {
      return execroot.status();
    }
    directory = *std::move(execroot);
  }

  absl::StatusOr<ActionRecords> current = BuildRecords(*proto, options.project_id);
  if (!current.ok()) {
    return current.status();
  }

  if (options.sidecar_path.empty()) {
    // No cache: scan every action (when a scanner is configured).
    if (options.scanner) {
      for (ActionRecord& record : *current->mutable_records()) {
        ScanHeaders(record, directory, options.scanner);
      }
    }
    return cdb::Write(options.output_path, EntriesFromRecords(*current, directory));
  }

  const absl::StatusOr<ActionRecords> stored = sidecar::Load(options.sidecar_path);
  if (!stored.ok()) {
    return stored.status();
  }

  // Scan added/changed actions, plus unchanged actions whose cached headers (or
  // source) have been edited on disk since the scan was recorded. Truly
  // unchanged actions reuse the stored record's cached headers via MergeRecords
  // below — the incremental-refresh win. `rescanned` records which keys carry a
  // fresh scan so the merge prefers the current record over the stale cache.
  absl::flat_hash_set<std::string_view> rescanned;
  if (options.scanner) {
    for (ActionRecord& record : *current->mutable_records()) {
      const ActionRecord* reusable = sidecar::FindReusableRecord(*stored, record, options.project_id);
      if (reusable == nullptr || CachedScanIsStale(*reusable, directory)) {
        ScanHeaders(record, directory, options.scanner);
        rescanned.insert(record.action_key());
      }
    }
  }

  ActionRecords merged = sidecar::MergeRecords(*stored, *current, options.project_id, rescanned);

  // Stamp written_at on the rows this refresh owns — added, changed, and reused
  // alike — so prune can GC projects that have stopped refreshing. Other
  // projects' rows keep their own timestamps. See CARVE_DESIGN.md section 4.4.
  if (options.clock) {
    const std::int64_t now = options.clock();
    for (ActionRecord& record : *merged.mutable_records()) {
      if (record.project_id() == options.project_id) {
        record.set_written_at(now);
      }
    }
  }

  if (const absl::Status saved = sidecar::Save(options.sidecar_path, merged); !saved.ok()) {
    return saved;
  }

  // Persist the header -> owning-action index alongside the entries sidecar
  // (the design's second cache file). It is a deterministic projection of the
  // merged records, so it is rebuilt from scratch each refresh — the reverse
  // lookup that maps an edited header to the action(s) to re-scan. See
  // CARVE_DESIGN.md sections 4.4-4.5.
  const std::filesystem::path index_path =
      std::filesystem::path(options.sidecar_path).parent_path() / "headers-index.binpb";
  if (const absl::Status saved = sidecar::SaveHeaderIndex(index_path, sidecar::BuildHeaderIndex(merged)); !saved.ok()) {
    return saved;
  }

  return cdb::Write(options.output_path, EntriesFromRecords(merged, directory));
}

}  // namespace carve::refresh
