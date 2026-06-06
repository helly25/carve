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

#ifndef CARVE_CLI_CLI_H_
#define CARVE_CLI_CLI_H_

#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace carve::cli {

// The subcommands carve recognizes. See CARVE_DESIGN.md section 6 for the flag
// surface behind each.
enum class Subcommand {
  kRefresh,    // Layer A entry point.
  kAggregate,  // Layer C aggregation.
  kShard,      // Layer C per-action invocation.
  kPrune,      // Sidecar garbage collection.
};

// Returns the canonical token for `cmd` (e.g. "refresh"). The result round-trips
// through `ParseSubcommand`.
[[nodiscard]] std::string_view SubcommandName(Subcommand cmd);

// Parses a leading subcommand token. Returns `InvalidArgumentError` for an empty
// or unrecognized token.
[[nodiscard]] absl::StatusOr<Subcommand> ParseSubcommand(std::string_view token);

// Dispatches `cmd` with its remaining positional arguments (the subcommand token
// already removed). Handlers are not implemented yet, so this returns
// `UnimplementedError`; it exists so the dispatch boundary is testable before
// the modules behind each subcommand land.
[[nodiscard]] absl::Status Dispatch(Subcommand cmd, absl::Span<const std::string_view> args);

}  // namespace carve::cli

#endif  // CARVE_CLI_CLI_H_
