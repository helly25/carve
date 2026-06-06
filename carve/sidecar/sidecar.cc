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

KeyDiff DiffActionKeys(const ActionRecords& stored, absl::Span<const std::string> current_keys) {
  absl::flat_hash_set<std::string_view> stored_set;
  stored_set.reserve(static_cast<std::size_t>(stored.records_size()));
  for (const ActionRecord& record : stored.records()) {
    stored_set.insert(record.action_key());
  }
  const absl::flat_hash_set<std::string_view> current_set(current_keys.begin(),
                                                          current_keys.end());

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

}  // namespace carve::sidecar
