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

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace carve::cli {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;

TEST(SubcommandTest, NamesRoundTripThroughParse) {
  for (const Subcommand cmd : {Subcommand::kRefresh, Subcommand::kAggregate, Subcommand::kShard, Subcommand::kPrune}) {
    EXPECT_THAT(ParseSubcommand(SubcommandName(cmd)), IsOkAndHolds(cmd));
  }
}

TEST(SubcommandTest, KnownTokensParse) {
  EXPECT_THAT(ParseSubcommand("refresh"), IsOkAndHolds(Subcommand::kRefresh));
  EXPECT_THAT(ParseSubcommand("aggregate"), IsOkAndHolds(Subcommand::kAggregate));
  EXPECT_THAT(ParseSubcommand("shard"), IsOkAndHolds(Subcommand::kShard));
  EXPECT_THAT(ParseSubcommand("prune"), IsOkAndHolds(Subcommand::kPrune));
}

TEST(SubcommandTest, UnknownTokenIsInvalidArgument) {
  EXPECT_THAT(ParseSubcommand("frobnicate"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(SubcommandTest, EmptyTokenIsInvalidArgument) {
  EXPECT_THAT(ParseSubcommand(""), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(DispatchTest, KnownSubcommandIsUnimplementedForNow) {
  EXPECT_THAT(Dispatch(Subcommand::kRefresh, {}), StatusIs(absl::StatusCode::kUnimplemented));
}

}  // namespace
}  // namespace carve::cli
