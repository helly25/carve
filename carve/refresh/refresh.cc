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

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "carve/aquery/aquery.h"
#include "carve/cdb/cdb.h"
#include "carve/command/command.h"
#include "carve/io/io.h"
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
// the source operand. Returns nullopt when no source can be identified (such an
// action cannot form a valid compilation-database entry).
std::optional<ActionRecord> MakeRecord(const aquery::CompileAction& action) {
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
  // Edition 2024 fields have explicit presence, so only set the output when it
  // exists; otherwise an empty value would serialize and differ from a record
  // that never had one.
  if (!action.primary_output.empty()) {
    record.set_primary_output(action.primary_output);
  }
  return record;
}

// Builds the current action records from serialized aquery bytes.
absl::StatusOr<ActionRecords> BuildRecords(std::string_view aquery_proto) {
  absl::StatusOr<std::vector<aquery::CompileAction>> actions =
      aquery::ParseCompileActions(aquery_proto);
  if (!actions.ok()) {
    return actions.status();
  }
  ActionRecords records;
  for (const aquery::CompileAction& action : *actions) {
    std::optional<ActionRecord> record = MakeRecord(action);
    if (record.has_value()) {
      *records.add_records() = *std::move(record);
    }
  }
  return records;
}

// Projects action records into compilation-database entries.
std::vector<cdb::CompileCommand> EntriesFromRecords(const ActionRecords& records,
                                                    std::string_view directory) {
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
    entries.push_back(cdb::CompileCommand{
        .directory = std::string(directory),
        .file = AbsoluteFile(directory, record.sources(0)),
        .arguments = std::move(arguments),
        .output = std::string(record.primary_output()),
    });
  }
  return entries;
}

}  // namespace

absl::StatusOr<std::vector<cdb::CompileCommand>> BuildEntries(std::string_view aquery_proto,
                                                              const Options& options) {
  const absl::StatusOr<ActionRecords> records = BuildRecords(aquery_proto);
  if (!records.ok()) {
    return records.status();
  }
  return EntriesFromRecords(*records, options.directory);
}

absl::Status RunRefresh(const FileOptions& options) {
  const absl::StatusOr<std::string> proto = io::ReadFile(options.aquery_proto_path);
  if (!proto.ok()) {
    return proto.status();
  }
  absl::StatusOr<ActionRecords> current = BuildRecords(*proto);
  if (!current.ok()) {
    return current.status();
  }

  ActionRecords merged;
  if (options.sidecar_path.empty()) {
    merged = *std::move(current);
  } else {
    const absl::StatusOr<ActionRecords> stored = sidecar::Load(options.sidecar_path);
    if (!stored.ok()) {
      return stored.status();
    }
    merged = sidecar::MergeRecords(*stored, *current);
    if (const absl::Status saved = sidecar::Save(options.sidecar_path, merged); !saved.ok()) {
      return saved;
    }
  }

  return cdb::Write(options.output_path, EntriesFromRecords(merged, options.directory));
}

}  // namespace carve::refresh
