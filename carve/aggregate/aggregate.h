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

#ifndef CARVE_AGGREGATE_AGGREGATE_H_
#define CARVE_AGGREGATE_AGGREGATE_H_

#include <filesystem>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "carve/sidecar/carve.pb.h"

namespace carve::aggregate {

// Combines records from several independently-produced sidecars into one set.
// Records are de-duplicated by (project_id, action_key) — the natural identity
// of an action across shards — keeping the most-recently-written record on a
// collision (highest `written_at`; an unstamped `written_at == 0` therefore
// loses to any stamped record). The result is sorted by (project_id,
// action_key) for determinism, and `schema_version` is taken from the first
// input that sets one. Pure: no filesystem access.
//
// This is the offline counterpart to `sidecar::MergeRecords`: where Merge folds
// one project's freshly-built records into a stored sidecar (preserving other
// projects), Combine unions many complete sidecars that were produced
// separately (e.g. parallel build shards of one workspace) into a single set.
[[nodiscard]] ActionRecords Combine(absl::Span<const ActionRecords> inputs);

// Loads each sidecar in `sidecar_paths`, combines them (see `Combine`), projects
// the result into a compilation database whose sources are resolved against
// `directory` (the shared execroot the sidecars' sources are relative to), and
// atomically writes the JSON database to `output_path`. Returns the number of
// entries written.
//
// A missing sidecar contributes no records and is not an error (a shard that
// produced nothing); a sidecar that exists but does not parse is an error.
[[nodiscard]] absl::StatusOr<int> RunAggregate(
    absl::Span<const std::filesystem::path> sidecar_paths,
    const std::filesystem::path& output_path,
    std::string_view directory);

}  // namespace carve::aggregate

#endif  // CARVE_AGGREGATE_AGGREGATE_H_
