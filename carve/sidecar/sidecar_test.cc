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

#include "absl/status/status_matchers.h"
#include "carve/sidecar/carve.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace carve::sidecar {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Property;

ActionRecords MakeRecords(const std::vector<std::string>& keys) {
  ActionRecords records;
  for (const std::string& key : keys) {
    records.add_records()->set_action_key(key);
  }
  return records;
}

TEST(LoadTest, MissingFileYieldsEmptyRecords) {
  EXPECT_THAT(Load("/no/such/carve/sidecar.binpb"),
              IsOkAndHolds(Property(&ActionRecords::records_size, Eq(0))));
}

TEST(SaveLoadTest, RoundTripsContent) {
  ActionRecords records = MakeRecords({"k1"});
  records.mutable_records(0)->add_sources("a.cc");
  records.mutable_records(0)->add_command("clang");
  const std::filesystem::path path =
      std::filesystem::path(::testing::TempDir()) / "carve_sidecar" / "entries.binpb";
  std::filesystem::remove_all(path.parent_path());

  ASSERT_THAT(Save(path, records), IsOk());
  EXPECT_THAT(Load(path),
              IsOkAndHolds(Property(&ActionRecords::SerializeAsString,
                                    Eq(records.SerializeAsString()))));
}

TEST(DiffActionKeysTest, PartitionsAddedRemovedCommon) {
  const ActionRecords stored = MakeRecords({"k1", "k2"});
  const std::vector<std::string> current = {"k2", "k3"};
  EXPECT_THAT(DiffActionKeys(stored, current),
              AllOf(Field(&KeyDiff::added, ElementsAre("k3")),
                    Field(&KeyDiff::removed, ElementsAre("k1")),
                    Field(&KeyDiff::common, ElementsAre("k2"))));
}

TEST(DiffActionKeysTest, DeduplicatesAndSortsCurrentKeys) {
  const ActionRecords stored = MakeRecords({});
  const std::vector<std::string> current = {"b", "a", "b"};
  EXPECT_THAT(DiffActionKeys(stored, current),
              AllOf(Field(&KeyDiff::added, ElementsAre("a", "b")),
                    Field(&KeyDiff::removed, IsEmpty()),
                    Field(&KeyDiff::common, IsEmpty())));
}

ActionRecord* AddRecord(ActionRecords& records, std::string_view key,
                        const std::vector<std::string>& command) {
  ActionRecord* record = records.add_records();
  record->set_action_key(std::string(key));
  for (const std::string& arg : command) {
    record->add_command(arg);
  }
  return record;
}

TEST(MergeRecordsTest, UnchangedActionKeepsStoredCachedFields) {
  ActionRecords stored;
  AddRecord(stored, "k1", {"clang", "-c", "a.cc"})->add_headers("cached.h");

  ActionRecords current;
  AddRecord(current, "k1", {"clang", "-c", "a.cc"});  // Same command, no headers.

  // The stored record (with its cached header) is preserved verbatim.
  EXPECT_THAT(MergeRecords(stored, current).SerializeAsString(), Eq(stored.SerializeAsString()));
}

TEST(MergeRecordsTest, ChangedCommandUsesCurrentRecord) {
  ActionRecords stored;
  AddRecord(stored, "k1", {"clang", "old"})->add_headers("cached.h");

  ActionRecords current;
  AddRecord(current, "k1", {"clang", "new"});

  EXPECT_THAT(MergeRecords(stored, current).SerializeAsString(), Eq(current.SerializeAsString()));
}

TEST(MergeRecordsTest, DropsRemovedAndIncludesAddedSortedByKey) {
  ActionRecords stored;
  AddRecord(stored, "k1", {"clang"});  // Absent from current -> removed.

  ActionRecords current;
  AddRecord(current, "k3", {"clang"});
  AddRecord(current, "k2", {"clang"});

  ActionRecords expected;
  AddRecord(expected, "k2", {"clang"});
  AddRecord(expected, "k3", {"clang"});

  EXPECT_THAT(MergeRecords(stored, current).SerializeAsString(), Eq(expected.SerializeAsString()));
}

}  // namespace
}  // namespace carve::sidecar
