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

#include <filesystem>

#include "carve/sidecar/carve.pb.h"
#include "carve/sidecar/sidecar.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/proto/matchers.h"
#include "mbo/proto/parse_text_proto.h"
#include "mbo/testing/status.h"

namespace carve::prune {
namespace {

using ::mbo::proto::EqualsProto;
using ::mbo::proto::ParseTextProtoOrDie;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::testing::Eq;

TEST(PruneRecordsTest, DropsStampedRecordsOlderThanCutoffKeepsRest) {
  const ActionRecords records = ParseTextProtoOrDie(
      R"pb(records { action_key: "old" written_at: 100 }
           records { action_key: "fresh" written_at: 300 }
           records { action_key: "exactly_cutoff" written_at: 200 }
           records { action_key: "unstamped" }
           schema_version: 1)pb");

  // cutoff = 200: `old` (100) is dropped; `fresh` (300) and `exactly_cutoff`
  // (>= cutoff) stay; `unstamped` (written_at 0) is always kept; schema_version
  // is preserved.
  EXPECT_THAT(
      PruneRecords(records, /*cutoff=*/200), EqualsProto(R"pb(records { action_key: "fresh" written_at: 300 }
                                                              records { action_key: "exactly_cutoff" written_at: 200 }
                                                              records { action_key: "unstamped" }
                                                              schema_version: 1)pb"));
}

TEST(PruneRecordsTest, EmptyStaysEmpty) {
  EXPECT_THAT(PruneRecords(ActionRecords(), /*cutoff=*/1'000), EqualsProto(""));
}

TEST(RunPruneTest, MissingSidecarRemovesNothing) {
  EXPECT_THAT(RunPrune("/no/such/carve/sidecar.binpb", /*cutoff=*/1'000), IsOkAndHolds(Eq(0)));
}

TEST(RunPruneTest, RewritesSidecarWithoutStaleRecords) {
  const std::filesystem::path path = std::filesystem::path(::testing::TempDir()) / "carve_prune" / "entries.binpb";
  std::filesystem::remove_all(path.parent_path());
  const ActionRecords seed = ParseTextProtoOrDie(
      R"pb(records { action_key: "old" written_at: 10 }
           records { action_key: "fresh" written_at: 500 })pb");
  ASSERT_THAT(sidecar::Save(path, seed), IsOk());

  // cutoff = 100 drops `old` (10) and keeps `fresh` (500).
  EXPECT_THAT(RunPrune(path, /*cutoff=*/100), IsOkAndHolds(Eq(1)));
  EXPECT_THAT(sidecar::Load(path), IsOkAndHolds(EqualsProto(R"pb(records { action_key: "fresh" written_at: 500 })pb")));
}

}  // namespace
}  // namespace carve::prune
