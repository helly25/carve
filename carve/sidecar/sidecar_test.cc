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

#include "carve/sidecar/sidecar.h"

#include <filesystem>
#include <string>
#include <vector>

#include "carve/sidecar/carve.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/proto/matchers.h"
#include "mbo/proto/parse_text_proto.h"
#include "mbo/testing/status.h"

namespace carve::sidecar {
namespace {

using ::mbo::proto::EqualsProto;
using ::mbo::proto::ParseTextProtoOrDie;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::Pointee;

TEST(LoadTest, MissingFileYieldsEmptyRecords) {
  EXPECT_THAT(Load("/no/such/carve/sidecar.binpb"), IsOkAndHolds(EqualsProto("")));
}

TEST(SaveLoadTest, RoundTripsContent) {
  const ActionRecords records =
      ParseTextProtoOrDie(R"pb(records { action_key: "k1" sources: "a.cc" command: "clang" })pb");
  const std::filesystem::path path = std::filesystem::path(::testing::TempDir()) / "carve_sidecar" / "entries.binpb";
  std::filesystem::remove_all(path.parent_path());

  ASSERT_THAT(Save(path, records), IsOk());
  EXPECT_THAT(
      Load(path), IsOkAndHolds(EqualsProto(R"pb(records { action_key: "k1" sources: "a.cc" command: "clang" })pb")));
}

TEST(DiffActionKeysTest, PartitionsAddedRemovedCommon) {
  const ActionRecords stored = ParseTextProtoOrDie(  // NL
      R"pb(
        records { action_key: "k1" }
        records { action_key: "k2" })pb");
  const std::vector<std::string> current = {"k2", "k3"};
  EXPECT_THAT(
      DiffActionKeys(stored, current),
      AllOf(
          Field(&KeyDiff::added, ElementsAre("k3")), Field(&KeyDiff::removed, ElementsAre("k1")),
          Field(&KeyDiff::common, ElementsAre("k2"))));
}

TEST(DiffActionKeysTest, DeduplicatesAndSortsCurrentKeys) {
  const std::vector<std::string> current = {"b", "a", "b"};
  EXPECT_THAT(
      DiffActionKeys(ActionRecords(), current),
      AllOf(
          Field(&KeyDiff::added, ElementsAre("a", "b")), Field(&KeyDiff::removed, IsEmpty()),
          Field(&KeyDiff::common, IsEmpty())));
}

TEST(MergeRecordsTest, UnchangedActionKeepsStoredCachedFields) {
  const ActionRecords stored = ParseTextProtoOrDie(
      R"pb(records { action_key: "k1" command: "clang" command: "-c" command: "a.cc" headers: "cached.h" })pb");
  // Same command, no headers.
  const ActionRecords current =
      ParseTextProtoOrDie(R"pb(records { action_key: "k1" command: "clang" command: "-c" command: "a.cc" })pb");

  // The stored record (with its cached header) is preserved verbatim.
  EXPECT_THAT(MergeRecords(stored, current, "", {}), EqualsProto(stored));
}

TEST(MergeRecordsTest, ChangedCommandUsesCurrentRecord) {
  const ActionRecords stored =
      ParseTextProtoOrDie(R"pb(records { action_key: "k1" command: "clang" command: "old" headers: "cached.h" })pb");
  const ActionRecords current =
      ParseTextProtoOrDie(R"pb(records { action_key: "k1" command: "clang" command: "new" })pb");

  EXPECT_THAT(MergeRecords(stored, current, "", {}), EqualsProto(current));
}

TEST(MergeRecordsTest, DropsRemovedAndIncludesAddedSortedByKey) {
  // k1 is absent from current -> removed.
  const ActionRecords stored = ParseTextProtoOrDie(R"pb(records { action_key: "k1" command: "clang" })pb");
  const ActionRecords current = ParseTextProtoOrDie(  // NL
      R"pb(
        records { action_key: "k3" command: "clang" }
        records { action_key: "k2" command: "clang" })pb");

  EXPECT_THAT(  // NL
      MergeRecords(stored, current, "", {}),
      EqualsProto(  // NL
          R"pb(
            records { action_key: "k2" command: "clang" }
            records { action_key: "k3" command: "clang" })pb"));
}

TEST(MergeRecordsTest, OtherProjectsArePreservedUntouched) {
  const ActionRecords stored = ParseTextProtoOrDie(
      R"pb(
        records { action_key: "k1" command: "clang" command: "a" project_id: "A" }
        records { action_key: "k2" command: "clang" command: "b" project_id: "B" })pb");
  // Refresh project A: k1's command changed; B must be left alone.
  const ActionRecords current =
      ParseTextProtoOrDie(R"pb(records { action_key: "k1" command: "clang" command: "a2" project_id: "A" })pb");

  EXPECT_THAT(
      MergeRecords(stored, current, "A", {}),
      EqualsProto(  // NL
          R"pb(
            records { action_key: "k1" command: "clang" command: "a2" project_id: "A" }
            records { action_key: "k2" command: "clang" command: "b" project_id: "B" })pb"));
}

TEST(MergeRecordsTest, OwnProjectRemovedActionsAreDroppedWithoutTouchingOthers) {
  const ActionRecords stored = ParseTextProtoOrDie(
      R"pb(
        records { action_key: "k1" command: "clang" project_id: "A" }
        records { action_key: "k2" command: "clang" project_id: "A" }
        records { action_key: "k3" command: "clang" project_id: "B" })pb");
  // Refresh project A keeps only k1; k2 (own, removed) is dropped, k3 (B) stays.
  const ActionRecords current =
      ParseTextProtoOrDie(R"pb(records { action_key: "k1" command: "clang" project_id: "A" })pb");

  EXPECT_THAT(
      MergeRecords(stored, current, "A", {}),
      EqualsProto(  // NL
          R"pb(
            records { action_key: "k1" command: "clang" project_id: "A" }
            records { action_key: "k3" command: "clang" project_id: "B" })pb"));
}

TEST(FindReusableRecordTest, MatchesOnlyOnKeyCommandAndProject) {
  const ActionRecords stored = ParseTextProtoOrDie(
      R"pb(records { action_key: "k1" command: "clang" command: "a.cc" project_id: "A" headers: "cached.h" })pb");
  const ActionRecord same =
      ParseTextProtoOrDie(R"pb(action_key: "k1" command: "clang" command: "a.cc" project_id: "A")pb");
  const ActionRecord other_command =
      ParseTextProtoOrDie(R"pb(action_key: "k1" command: "clang" command: "b.cc" project_id: "A")pb");
  const ActionRecord other_key =
      ParseTextProtoOrDie(R"pb(action_key: "k2" command: "clang" command: "a.cc" project_id: "A")pb");

  // The match returns the stored record (so the caller can read its cached
  // fields); a changed command, a new key, or another project finds nothing.
  EXPECT_THAT(  // NL
      FindReusableRecord(stored, same, "A"),
      Pointee(EqualsProto(  // NL
          R"pb(
            action_key: "k1"
            command: "clang"
            command: "a.cc"
            project_id: "A"
            headers: "cached.h")pb")));
  EXPECT_THAT(FindReusableRecord(stored, other_command, "A"), IsNull());  // command changed
  EXPECT_THAT(FindReusableRecord(stored, other_key, "A"), IsNull());      // new action
  EXPECT_THAT(FindReusableRecord(stored, same, "B"), IsNull());           // other project
}

TEST(MergeRecordsTest, RescannedKeyTakesCurrentEvenWhenCommandMatches) {
  // Same key and command, but the cached header on the stored record is stale
  // and the action was re-scanned, so its key is in `rescanned`.
  const ActionRecords stored =
      ParseTextProtoOrDie(R"pb(records { action_key: "k1" command: "clang" command: "a.cc" headers: "stale.h" })pb");
  const ActionRecords current =
      ParseTextProtoOrDie(R"pb(records { action_key: "k1" command: "clang" command: "a.cc" headers: "fresh.h" })pb");

  // Without `rescanned` the stale stored record would be kept; naming the key
  // forces the freshly re-scanned current record instead.
  EXPECT_THAT(MergeRecords(stored, current, "", {}), EqualsProto(stored));
  EXPECT_THAT(MergeRecords(stored, current, "", {"k1"}), EqualsProto(current));
}

TEST(BuildHeaderIndexTest, EmptyRecordsYieldEmptyIndexWithSchemaVersion) {
  EXPECT_THAT(BuildHeaderIndex(ActionRecords()), EqualsProto(R"pb(schema_version: 1)pb"));
}

TEST(BuildHeaderIndexTest, MapsHeadersToSortedOwners) {
  const ActionRecords records = ParseTextProtoOrDie(
      R"pb(
        records { action_key: "k2" headers: "b.h" headers: "a.h" }
        records { action_key: "k1" headers: "a.h" })pb");

  // a.h is owned by k1 and k2 (sorted, k1 canonical); b.h only by k2; owners
  // sorted by header_path.
  EXPECT_THAT(  // NL
      BuildHeaderIndex(records),
      EqualsProto(  // NL
          R"pb(
            owners { header_path: "a.h" action_keys: "k1" action_keys: "k2" }
            owners { header_path: "b.h" action_keys: "k2" }
            schema_version: 1)pb"));
}

TEST(LoadHeaderIndexTest, MissingFileYieldsEmptyIndex) {
  EXPECT_THAT(LoadHeaderIndex("/no/such/carve/headers-index.binpb"), IsOkAndHolds(EqualsProto("")));
}

TEST(SaveHeaderIndexTest, RoundTripsContent) {
  const HeaderIndex index = ParseTextProtoOrDie(  // NL
      R"pb(
        owners { header_path: "a.h" action_keys: "k1" }
        schema_version: 1)pb");
  const std::filesystem::path path =
      std::filesystem::path(::testing::TempDir()) / "carve_header_index" / "headers-index.binpb";
  std::filesystem::remove_all(path.parent_path());

  ASSERT_THAT(SaveHeaderIndex(path, index), IsOk());
  EXPECT_THAT(  // NL
      LoadHeaderIndex(path),
      IsOkAndHolds(EqualsProto(  // NL
          R"pb(
            owners { header_path: "a.h" action_keys: "k1" }
            schema_version: 1)pb")));
}

}  // namespace
}  // namespace carve::sidecar
