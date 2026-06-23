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

#include "carve/sidecar/sidecar.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "carve/io/io.h"
#include "carve/sidecar/carve.pb.h"

namespace carve::sidecar {

absl::StatusOr<ActionRecords> Load(const std::filesystem::path& path) {
  const absl::StatusOr<std::string> bytes = io::ReadFile(path);
  if (!bytes.ok()) {
    if (absl::IsNotFound(bytes.status())) {
      return ActionRecords{};  // First run: no sidecar yet.
    }
    return bytes.status();
  }
  ActionRecords records;
  if (!records.ParseFromString(*bytes)) {
    return absl::InvalidArgumentError("sidecar is not a valid ActionRecords proto");
  }
  return records;
}

absl::Status Save(const std::filesystem::path& path, const ActionRecords& records) {
  return io::WriteAtomically(path, records.SerializeAsString());
}

absl::StatusOr<HeaderIndex> LoadHeaderIndex(const std::filesystem::path& path) {
  const absl::StatusOr<std::string> bytes = io::ReadFile(path);
  if (!bytes.ok()) {
    if (absl::IsNotFound(bytes.status())) {
      return HeaderIndex{};  // Not yet built: rebuilt on the next refresh.
    }
    return bytes.status();
  }
  HeaderIndex index;
  if (!index.ParseFromString(*bytes)) {
    return absl::InvalidArgumentError("header index is not a valid HeaderIndex proto");
  }
  return index;
}

absl::Status SaveHeaderIndex(const std::filesystem::path& path, const HeaderIndex& index) {
  return io::WriteAtomically(path, index.SerializeAsString());
}

KeyDiff DiffActionKeys(const ActionRecords& stored, absl::Span<const std::string> current_keys) {
  absl::flat_hash_set<std::string_view> stored_set;
  stored_set.reserve(static_cast<std::size_t>(stored.records_size()));
  for (const ActionRecord& record : stored.records()) {
    stored_set.insert(record.action_key());
  }
  const absl::flat_hash_set<std::string_view> current_set(current_keys.begin(), current_keys.end());

  KeyDiff diff;
  for (const std::string_view key : current_set) {
    (stored_set.contains(key) ? diff.common : diff.added).emplace_back(key);
  }
  for (const std::string_view key : stored_set) {
    if (!current_set.contains(key)) {
      diff.removed.emplace_back(key);
    }
  }
  std::sort(diff.added.begin(), diff.added.end());
  std::sort(diff.removed.begin(), diff.removed.end());
  std::sort(diff.common.begin(), diff.common.end());
  return diff;
}

namespace {

bool SameCommand(const ActionRecord& lhs, const ActionRecord& rhs) {
  if (lhs.command_size() != rhs.command_size()) {
    return false;
  }
  for (int i = 0; i < lhs.command_size(); ++i) {
    if (lhs.command(i) != rhs.command(i)) {
      return false;
    }
  }
  return true;
}

}  // namespace

ActionRecords MergeRecords(const ActionRecords& stored, const ActionRecords& current, std::string_view project_id) {
  ActionRecords merged;
  // Records of other projects pass through untouched; index our project's
  // stored records by key for reuse.
  absl::flat_hash_map<std::string_view, const ActionRecord*> stored_own;
  for (const ActionRecord& record : stored.records()) {
    if (record.project_id() == project_id) {
      stored_own.emplace(record.action_key(), &record);
    } else {
      *merged.add_records() = record;
    }
  }

  for (const ActionRecord& cur : current.records()) {
    const auto it = stored_own.find(cur.action_key());
    if (it != stored_own.end() && SameCommand(*it->second, cur)) {
      *merged.add_records() = *it->second;  // Unchanged: keep cached fields.
    } else {
      *merged.add_records() = cur;  // Added or changed.
    }
  }

  std::sort(
      merged.mutable_records()->pointer_begin(), merged.mutable_records()->pointer_end(),
      [](const ActionRecord* lhs, const ActionRecord* rhs) {
        if (lhs->project_id() != rhs->project_id()) {
          return lhs->project_id() < rhs->project_id();
        }
        return lhs->action_key() < rhs->action_key();
      });
  return merged;
}

bool HasMatchingRecord(const ActionRecords& stored, const ActionRecord& candidate, std::string_view project_id) {
  for (const ActionRecord& record : stored.records()) {
    if (record.project_id() == project_id && record.action_key() == candidate.action_key()
        && SameCommand(record, candidate)) {
      return true;
    }
  }
  return false;
}

HeaderIndex BuildHeaderIndex(const ActionRecords& records) {
  // btree containers keep the index deterministic: owners sorted by header_path,
  // action_keys sorted (lex-min first = canonical owner). Keys are views into
  // `records`, which outlives this function.
  absl::btree_map<std::string_view, absl::btree_set<std::string_view>> owners;
  for (const ActionRecord& record : records.records()) {
    for (int i = 0; i < record.headers_size(); ++i) {
      owners[record.headers(i)].insert(record.action_key());
    }
  }

  HeaderIndex index;
  index.set_schema_version(kHeaderIndexSchemaVersion);
  for (const auto& [header_path, action_keys] : owners) {
    HeaderOwners* entry = index.add_owners();
    entry->set_header_path(header_path);
    for (const std::string_view action_key : action_keys) {
      entry->add_action_keys(action_key);
    }
  }
  return index;
}

}  // namespace carve::sidecar
