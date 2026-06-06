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
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

namespace carve::cli {
namespace {

TEST(SubcommandTest, NamesRoundTripThroughParse) {
  for (const Subcommand cmd :
       {Subcommand::kRefresh, Subcommand::kAggregate, Subcommand::kShard, Subcommand::kPrune}) {
    const absl::StatusOr<Subcommand> parsed = ParseSubcommand(SubcommandName(cmd));
    ASSERT_TRUE(parsed.ok()) << "name=" << SubcommandName(cmd);
    EXPECT_EQ(*parsed, cmd);
  }
}

TEST(SubcommandTest, KnownTokensParse) {
  EXPECT_EQ(*ParseSubcommand("refresh"), Subcommand::kRefresh);
  EXPECT_EQ(*ParseSubcommand("aggregate"), Subcommand::kAggregate);
  EXPECT_EQ(*ParseSubcommand("shard"), Subcommand::kShard);
  EXPECT_EQ(*ParseSubcommand("prune"), Subcommand::kPrune);
}

TEST(SubcommandTest, UnknownTokenIsInvalidArgument) {
  EXPECT_EQ(ParseSubcommand("frobnicate").status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(SubcommandTest, EmptyTokenIsInvalidArgument) {
  EXPECT_EQ(ParseSubcommand("").status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(DispatchTest, KnownSubcommandIsUnimplementedForNow) {
  const std::vector<std::string_view> args;
  EXPECT_EQ(Dispatch(Subcommand::kRefresh, args).code(), absl::StatusCode::kUnimplemented);
}

}  // namespace
}  // namespace carve::cli
