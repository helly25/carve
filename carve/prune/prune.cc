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

#include "carve/prune/prune.h"

#include <cstdint>
#include <filesystem>

#include "absl/status/statusor.h"
#include "carve/sidecar/carve.pb.h"
#include "carve/sidecar/sidecar.h"
#include "mbo/status/status_macros.h"

namespace carve::prune {

ActionRecords PruneRecords(const ActionRecords& records, std::int64_t cutoff) {
  ActionRecords kept = records;  // Copy first to preserve schema_version etc.
  kept.clear_records();
  for (const ActionRecord& record : records.records()) {
    if (record.written_at() == 0 || record.written_at() >= cutoff) {
      *kept.add_records() = record;
    }
  }
  return kept;
}

absl::StatusOr<int> RunPrune(const std::filesystem::path& path, std::int64_t cutoff) {
  MBO_ASSIGN_OR_RETURN(const ActionRecords stored, sidecar::Load(path));
  const ActionRecords pruned = PruneRecords(stored, cutoff);
  const int removed = stored.records_size() - pruned.records_size();
  // Only rewrite when something changed: this keeps prune idempotent and avoids
  // creating a sidecar for a path that has none (Load treats missing as empty).
  if (removed > 0) {
    MBO_RETURN_IF_ERROR(sidecar::Save(path, pruned));
  }
  return removed;
}

}  // namespace carve::prune
