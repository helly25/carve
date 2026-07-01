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

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
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

// Loads the header index from `path`. A missing file yields an empty
// `HeaderIndex` (it is simply rebuilt on the next refresh, not an error).
// Returns `InvalidArgumentError` if the file exists but does not parse.
[[nodiscard]] absl::StatusOr<HeaderIndex> LoadHeaderIndex(const std::filesystem::path& path);

// Atomically writes `index` to `path` as a binary proto.
[[nodiscard]] absl::Status SaveHeaderIndex(const std::filesystem::path& path, const HeaderIndex& index);

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
[[nodiscard]] KeyDiff DiffActionKeys(const ActionRecords& stored, absl::Span<const std::string> current_keys);

// Merges freshly-built `current` records (all belonging to `project_id`) into
// `stored`, the basis of the shared cross-project CDB. Records of OTHER projects
// are preserved untouched. Within `project_id`: a stored record whose key AND
// command match a current record is kept - preserving cached fields (e.g.
// scan-deps-resolved headers) the current record does not yet carry - UNLESS the
// current record's `action_key` is in `rescanned`, in which case the current
// record is authoritative (it was just re-scanned, e.g. because a cached header
// changed on disk) and is used as-is. Otherwise (a new key or a changed command)
// the current record is used; and own-project records absent from `current` are
// dropped. The result is sorted by (project_id, action_key) for determinism.
[[nodiscard]] ActionRecords MergeRecords(
    const ActionRecords& stored,
    const ActionRecords& current,
    std::string_view project_id,
    const absl::flat_hash_set<std::string_view>& rescanned);

// Returns the stored record in `project_id` whose `action_key` AND command match
// `candidate` - the record `MergeRecords` would reuse, letting a caller read its
// cached fields (resolved headers, `written_at`) to decide whether they are
// still valid - or nullptr if no such record exists (a new or changed action).
// The pointer is valid for the lifetime of `stored`.
[[nodiscard]] const ActionRecord* FindReusableRecord(
    const ActionRecords& stored,
    const ActionRecord& candidate,
    std::string_view project_id);

// Schema version stamped into a freshly built HeaderIndex; bump when the index
// layout changes so stale sidecars can be detected and rebuilt.
inline constexpr std::uint32_t kHeaderIndexSchemaVersion = 1;

// Builds the header -> owning-action index from `records`: every header in a
// record's `headers` maps to that record's `action_key`. Within each
// `HeaderOwners`, `action_keys` is sorted (the lex-min is the canonical owner),
// and `owners` is sorted by `header_path`. Deterministic for a given input.
// This is what lets an edited header be mapped to the action(s) to re-scan
// (CARVE_DESIGN.md section 4.5).
[[nodiscard]] HeaderIndex BuildHeaderIndex(const ActionRecords& records);

}  // namespace carve::sidecar

#endif  // CARVE_SIDECAR_SIDECAR_H_
