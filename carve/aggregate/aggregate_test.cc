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

#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <vector>

#include "carve/sidecar/carve.pb.h"
#include "carve/sidecar/sidecar.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/proto/matchers.h"
#include "mbo/proto/parse_text_proto.h"
#include "mbo/testing/status.h"

namespace carve::aggregate {
namespace {

using ::mbo::proto::EqualsProto;
using ::mbo::proto::ParseTextProtoOrDie;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::testing::Eq;
using ::testing::HasSubstr;

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

TEST(CombineTest, UnionsAndSortsByProjectThenActionKey) {
  const ActionRecords a = ParseTextProtoOrDie(R"pb(records { project_id: "p1" action_key: "b" })pb");
  const ActionRecords b = ParseTextProtoOrDie(
      R"pb(
        records { project_id: "p2" action_key: "a" }
        records { project_id: "p1" action_key: "a" })pb");

  const std::vector<ActionRecords> inputs = {a, b};
  EXPECT_THAT(  // NL
      Combine(inputs),
      EqualsProto(  // NL
          R"pb(
            records { project_id: "p1" action_key: "a" }
            records { project_id: "p1" action_key: "b" }
            records { project_id: "p2" action_key: "a" })pb"));
}

TEST(CombineTest, DedupsByIdentityKeepingMostRecentlyWritten) {
  const ActionRecords older =
      ParseTextProtoOrDie(R"pb(records { project_id: "p" action_key: "k" written_at: 100 sources: "old.cc" })pb");
  const ActionRecords newer =
      ParseTextProtoOrDie(R"pb(records { project_id: "p" action_key: "k" written_at: 200 sources: "new.cc" })pb");

  // The higher `written_at` wins regardless of input order.
  const std::vector<ActionRecords> forward = {older, newer};
  const std::vector<ActionRecords> backward = {newer, older};
  const auto* expected = R"pb(records { project_id: "p" action_key: "k" written_at: 200 sources: "new.cc" })pb";
  EXPECT_THAT(Combine(forward), EqualsProto(expected));
  EXPECT_THAT(Combine(backward), EqualsProto(expected));
}

TEST(CombineTest, CarriesSchemaVersionFromFirstInputThatSetsOne) {
  // The first input leaves schema_version unset (0); the second sets it.
  const ActionRecords first = ParseTextProtoOrDie(R"pb(records { project_id: "p" action_key: "a" })pb");
  const ActionRecords second = ParseTextProtoOrDie(R"pb(schema_version: 3)pb");

  const std::vector<ActionRecords> inputs = {first, second};
  EXPECT_THAT(  // NL
      Combine(inputs),
      EqualsProto(  // NL
          R"pb(
            records { project_id: "p" action_key: "a" }
            schema_version: 3)pb"));
}

TEST(CombineTest, EmptyInputsYieldEmpty) {
  EXPECT_THAT(Combine({}), EqualsProto(""));
  const std::vector<ActionRecords> empties = {ActionRecords(), ActionRecords()};
  EXPECT_THAT(Combine(empties), EqualsProto(""));
}

TEST(RunAggregateTest, MergesSidecarsIntoOneDatabase) {
  const std::filesystem::path dir = std::filesystem::path(::testing::TempDir()) / "carve_aggregate";
  std::filesystem::remove_all(dir);
  const std::filesystem::path shard1 = dir / "shard1.binpb";
  const std::filesystem::path shard2 = dir / "shard2.binpb";
  const std::filesystem::path out = dir / "compile_commands.json";

  ASSERT_THAT(
      sidecar::Save(
          shard1, ParseTextProtoOrDie(
                      R"pb(
                        records {
                          project_id: "p"
                          action_key: "a"
                          command: "clang"
                          command: "-c"
                          command: "carve/a.cc"
                          sources: "carve/a.cc"
                          primary_output: "bazel-out/a.o"
                        })pb")),
      IsOk());
  ASSERT_THAT(
      sidecar::Save(
          shard2, ParseTextProtoOrDie(
                      R"pb(
                        records {
                          project_id: "p"
                          action_key: "b"
                          command: "clang"
                          command: "-c"
                          command: "carve/b.cc"
                          sources: "carve/b.cc"
                          primary_output: "bazel-out/b.o"
                        })pb")),
      IsOk());

  const std::vector<std::filesystem::path> shards = {shard1, shard2};
  EXPECT_THAT(RunAggregate(shards, out, /*directory=*/"/exec/root"), IsOkAndHolds(Eq(2)));

  const std::string json = ReadFile(out);
  // `file` is execroot-relative; `directory` is the execroot clangd resolves it against.
  EXPECT_THAT(json, HasSubstr("\"file\": \"carve/a.cc\""));
  EXPECT_THAT(json, HasSubstr("\"file\": \"carve/b.cc\""));
  EXPECT_THAT(json, HasSubstr("\"directory\": \"/exec/root\""));
}

TEST(RunAggregateTest, MissingSidecarContributesNothing) {
  const std::filesystem::path dir = std::filesystem::path(::testing::TempDir()) / "carve_aggregate_missing";
  std::filesystem::remove_all(dir);
  const std::filesystem::path out = dir / "compile_commands.json";

  const std::vector<std::filesystem::path> shards = {dir / "absent.binpb"};
  EXPECT_THAT(RunAggregate(shards, out, /*directory=*/"/exec/root"), IsOkAndHolds(Eq(0)));
  EXPECT_THAT(ReadFile(out), Eq("[]\n"));
}

TEST(RunAggregateTest, WritesHeaderIndexFromShardHeaders) {
  const std::filesystem::path dir = std::filesystem::path(::testing::TempDir()) / "carve_aggregate_header_index";
  std::filesystem::remove_all(dir);
  const std::filesystem::path shard_z = dir / "shard_z.binpb";
  const std::filesystem::path shard_a = dir / "shard_a.binpb";
  const std::filesystem::path out = dir / "compile_commands.json";

  // Two record-headers shards. `shared.h` is included by both actions, so it is
  // owned by both; `z.h` only by "z". Shards are saved in a non-sorted order to
  // prove the index does not depend on input order.
  ASSERT_THAT(
      sidecar::Save(
          shard_z, ParseTextProtoOrDie(
                       R"pb(
                         records {
                           project_id: "p"
                           action_key: "z"
                           command: "clang"
                           command: "-c"
                           command: "carve/z.cc"
                           sources: "carve/z.cc"
                           headers: "carve/shared.h"
                           headers: "carve/z.h"
                           source_kind: ASPECT_M
                         })pb")),
      IsOk());
  ASSERT_THAT(
      sidecar::Save(
          shard_a, ParseTextProtoOrDie(
                       R"pb(
                         records {
                           project_id: "p"
                           action_key: "a"
                           command: "clang"
                           command: "-c"
                           command: "carve/a.cc"
                           sources: "carve/a.cc"
                           headers: "carve/shared.h"
                           source_kind: ASPECT_M
                         })pb")),
      IsOk());

  const std::vector<std::filesystem::path> shards = {shard_z, shard_a};
  EXPECT_THAT(RunAggregate(shards, out, /*directory=*/"/exec/root"), IsOkAndHolds(Eq(2)));

  // The index lands next to the database (same filename refresh uses). Owners are
  // sorted by header_path; a header owned by several actions lists all of them
  // with the lex-min ("a") first (the canonical owner).
  const std::filesystem::path index_path = dir / "headers-index.binpb";
  EXPECT_THAT(
      sidecar::LoadHeaderIndex(index_path),
      IsOkAndHolds(EqualsProto(  // NL
          R"pb(
            owners { header_path: "carve/shared.h" action_keys: "a" action_keys: "z" }
            owners { header_path: "carve/z.h" action_keys: "z" }
            schema_version: 1)pb")));
}

TEST(RunAggregateTest, WritesEmptyHeaderIndexWhenShardsCarryNoHeaders) {
  const std::filesystem::path dir = std::filesystem::path(::testing::TempDir()) / "carve_aggregate_no_headers";
  std::filesystem::remove_all(dir);
  const std::filesystem::path shard = dir / "shard.binpb";
  const std::filesystem::path out = dir / "compile_commands.json";

  // record_headers off: the shard carries a source but no headers.
  ASSERT_THAT(
      sidecar::Save(
          shard, ParseTextProtoOrDie(
                     R"pb(
                       records {
                         project_id: "p"
                         action_key: "a"
                         command: "clang"
                         command: "-c"
                         command: "carve/a.cc"
                         sources: "carve/a.cc"
                       })pb")),
      IsOk());

  const std::vector<std::filesystem::path> shards = {shard};
  EXPECT_THAT(RunAggregate(shards, out, /*directory=*/"/exec/root"), IsOkAndHolds(Eq(1)));

  // The index is written even with no headers - owners empty, schema stamped -
  // matching refresh, so a consumer always finds a current index.
  const std::filesystem::path index_path = dir / "headers-index.binpb";
  EXPECT_THAT(sidecar::LoadHeaderIndex(index_path), IsOkAndHolds(EqualsProto(R"pb(schema_version: 1)pb")));
}

}  // namespace
}  // namespace carve::aggregate
