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

#include "carve/cli/cli.h"

#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"

namespace carve::cli {

std::string_view SubcommandName(Subcommand cmd) {
  switch (cmd) {
    case Subcommand::kRefresh: return "refresh";
    case Subcommand::kAggregate: return "aggregate";
    case Subcommand::kShard: return "shard";
    case Subcommand::kPrune: return "prune";
  }
  return "";  // Unreachable for a valid enumerator; satisfies -Wreturn-type.
}

absl::StatusOr<Subcommand> ParseSubcommand(std::string_view token) {
  if (token == "refresh") {
    return Subcommand::kRefresh;
  }
  if (token == "aggregate") {
    return Subcommand::kAggregate;
  }
  if (token == "shard") {
    return Subcommand::kShard;
  }
  if (token == "prune") {
    return Subcommand::kPrune;
  }
  return absl::InvalidArgumentError(absl::StrCat("unknown subcommand: '", token, "'"));
}

absl::Status Dispatch(Subcommand cmd, absl::Span<const std::string_view> args) {
  static_cast<void>(args);  // Handlers consume these once implemented.
  return absl::UnimplementedError(absl::StrCat(SubcommandName(cmd), " is not implemented yet"));
}

}  // namespace carve::cli
