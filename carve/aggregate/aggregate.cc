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

#include "carve/aggregate/aggregate.h"

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "carve/cdb/cdb.h"
#include "carve/refresh/refresh.h"
#include "carve/sidecar/carve.pb.h"
#include "carve/sidecar/sidecar.h"
#include "mbo/status/status_macros.h"

namespace carve::aggregate {

ActionRecords Combine(absl::Span<const ActionRecords> inputs) {
  ActionRecords combined;
  // Carry the schema version from the first input that declares one; shards of
  // one build share a version, and an empty input simply does not vote.
  for (const ActionRecords& input : inputs) {
    if (input.schema_version() != 0) {
      combined.set_schema_version(input.schema_version());
      break;
    }
  }

  // De-duplicate by (project_id, action_key), keeping the most-recently-written
  // record. The keys are views into `inputs`, which outlive this function.
  absl::flat_hash_map<std::pair<std::string_view, std::string_view>, const ActionRecord*> best;
  for (const ActionRecords& input : inputs) {
    for (const ActionRecord& record : input.records()) {
      const auto [it, inserted] = best.try_emplace({record.project_id(), record.action_key()}, &record);
      if (!inserted && record.written_at() > it->second->written_at()) {
        it->second = &record;
      }
    }
  }

  // Materialize in a deterministic (project_id, action_key) order.
  std::vector<const ActionRecord*> ordered;
  ordered.reserve(best.size());
  for (const auto& [key, record] : best) {
    ordered.push_back(record);
  }
  std::sort(ordered.begin(), ordered.end(), [](const ActionRecord* lhs, const ActionRecord* rhs) {
    if (lhs->project_id() != rhs->project_id()) {
      return lhs->project_id() < rhs->project_id();
    }
    return lhs->action_key() < rhs->action_key();
  });
  for (const ActionRecord* record : ordered) {
    *combined.add_records() = *record;
  }
  return combined;
}

absl::StatusOr<int> RunAggregate(
    absl::Span<const std::filesystem::path> sidecar_paths,
    const std::filesystem::path& output_path,
    std::string_view directory) {
  std::vector<ActionRecords> loaded;
  loaded.reserve(sidecar_paths.size());
  for (const std::filesystem::path& path : sidecar_paths) {
    MBO_ASSIGN_OR_RETURN(ActionRecords records, sidecar::Load(path));
    loaded.push_back(std::move(records));
  }

  const ActionRecords combined = Combine(loaded);
  const std::vector<cdb::CompileCommand> entries = refresh::EntriesFromRecords(combined, directory);
  MBO_RETURN_IF_ERROR(cdb::Write(output_path, entries));

  // Persist the header -> owning-action index next to the database, matching
  // `refresh` (same builder, same `headers-index.binpb` filename). Where refresh
  // places it beside its single sidecar, aggregate has no one sidecar but does
  // have a single output database, so it uses the database's directory - the
  // "single directory next to the CDB" the two cache files share (CARVE_DESIGN.md
  // sections 4.4-4.5). When the merged shards carry no headers (record_headers
  // off) the index is empty apart from its schema version; it is written anyway,
  // as refresh does, so a consumer always finds a current index.
  const std::filesystem::path index_path = output_path.parent_path() / "headers-index.binpb";
  MBO_RETURN_IF_ERROR(sidecar::SaveHeaderIndex(index_path, sidecar::BuildHeaderIndex(combined)));
  return static_cast<int>(entries.size());
}

}  // namespace carve::aggregate
