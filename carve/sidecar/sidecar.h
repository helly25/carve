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

#ifndef CARVE_SIDECAR_SIDECAR_H_
#define CARVE_SIDECAR_SIDECAR_H_

#include <filesystem>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "carve/sidecar/carve.pb.h"

namespace carve::sidecar {

// Loads the action-records sidecar from `path`. A missing file yields an empty
// `ActionRecords` (a first run is not an error). Returns `InvalidArgumentError`
// if the file exists but does not parse as `ActionRecords`.
[[nodiscard]] absl::StatusOr<ActionRecords> Load(const std::filesystem::path& path);

// Atomically writes `records` to `path` as a binary proto.
[[nodiscard]] absl::Status Save(const std::filesystem::path& path, const ActionRecords& records);

// Partition of action keys between a stored sidecar and a current action set.
// This is the basis of incremental refresh: `added` actions are new work,
// `removed` actions are gone, `common` actions may be reused. All vectors are
// sorted and de-duplicated for determinism.
struct KeyDiff {
  std::vector<std::string> added;    // in current, not stored
  std::vector<std::string> removed;  // in stored, not current
  std::vector<std::string> common;   // in both
};

// Diffs the `action_key`s in `stored` against `current_keys`.
[[nodiscard]] KeyDiff DiffActionKeys(const ActionRecords& stored,
                                     absl::Span<const std::string> current_keys);

// Merges freshly-built `current` records against the `stored` sidecar. For an
// action whose key AND command both match a stored record, the stored record is
// kept — preserving cached fields (e.g. scan-deps-resolved headers) that the
// current record does not yet carry. Otherwise (a new key, or a changed
// command) the current record is used. Stored records whose key is absent from
// `current` are dropped. The result is sorted by `action_key` for determinism.
[[nodiscard]] ActionRecords MergeRecords(const ActionRecords& stored, const ActionRecords& current);

}  // namespace carve::sidecar

#endif  // CARVE_SIDECAR_SIDECAR_H_
