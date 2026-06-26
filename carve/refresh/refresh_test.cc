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

#include "carve/refresh/refresh.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "carve/cdb/cdb.h"
#include "carve/sidecar/carve.pb.h"
#include "carve/sidecar/sidecar.h"
#include "carve/third_party/bazel/analysis_v2.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/proto/matchers.h"
#include "mbo/proto/parse_text_proto.h"

namespace carve::refresh {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::mbo::proto::EqualsProto;
using ::mbo::proto::ParseTextProtoOrDie;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Property;

// Serializes `message` to `path` (creating parents), returning `path`.
template<typename Message>
std::filesystem::path WriteProto(const std::filesystem::path& path, const Message& message) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  const std::string bytes = message.SerializeAsString();
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return path;
}

// Writes `text` to `path` (creating parents), returning `path`. Used to put real
// files on disk so staleness `stat`s have something to read.
std::filesystem::path WriteText(const std::filesystem::path& path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  return path;
}

analysis::Action* AddCompile(analysis::ActionGraphContainer& container, std::string_view key) {
  analysis::Action* action = container.add_actions();
  action->set_mnemonic("CppCompile");
  action->set_action_key(std::string(key));
  return action;
}

TEST(BuildEntriesTest, MapsCompileActionToDeBazeledEntry) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  for (std::string_view arg : {"clang", "-fno-canonical-system-headers", "-c", "src/a.cc", "-o", "bazel-out/a.o"}) {
    compile->add_arguments(std::string(arg));
  }

  // `file` is made absolute against the directory; arguments stay exec-relative
  // and the de-Bazel transform dropped -fno-canonical-system-headers.
  EXPECT_THAT(
      BuildEntries(container.SerializeAsString(), Options{.directory = "/execroot/ws"}),
      IsOkAndHolds(ElementsAre(AllOf(
          Field(&cdb::CompileCommand::directory, Eq("/execroot/ws")),
          Field(&cdb::CompileCommand::file, Eq("/execroot/ws/src/a.cc")),
          Field(&cdb::CompileCommand::arguments, ElementsAre("clang", "-c", "src/a.cc", "-o", "bazel-out/a.o"))))));
}

TEST(BuildEntriesTest, AbsoluteSourcePathIsLeftUnchanged) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  compile->add_arguments("clang");
  compile->add_arguments("-c");
  compile->add_arguments("/abs/src/a.cc");

  EXPECT_THAT(
      BuildEntries(container.SerializeAsString(), Options{.directory = "/execroot/ws"}),
      IsOkAndHolds(ElementsAre(Field(&cdb::CompileCommand::file, Eq("/abs/src/a.cc")))));
}

TEST(BuildEntriesTest, EmptyDirectoryLeavesSourceRelative) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  compile->add_arguments("clang");
  compile->add_arguments("-c");
  compile->add_arguments("src/a.cc");

  EXPECT_THAT(
      BuildEntries(container.SerializeAsString(), Options{.directory = ""}),
      IsOkAndHolds(ElementsAre(Field(&cdb::CompileCommand::file, Eq("src/a.cc")))));
}

TEST(BuildEntriesTest, SkipsActionsWithoutADetectableSource) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  compile->add_arguments("clang");
  compile->add_arguments("--version");  // No source operand.

  EXPECT_THAT(BuildEntries(container.SerializeAsString(), Options{.directory = "/ws"}), IsOkAndHolds(IsEmpty()));
}

TEST(BuildEntriesTest, EmptyInputYieldsNoEntries) {
  EXPECT_THAT(BuildEntries("", Options{.directory = "/ws"}), IsOkAndHolds(IsEmpty()));
}

TEST(RunRefreshTest, ReadsProtoFileAndWritesCompileCommands) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  for (std::string_view arg : {"clang", "-c", "src/a.cc"}) {
    compile->add_arguments(std::string(arg));
  }

  const std::filesystem::path dir = std::filesystem::path(::testing::TempDir()) / "carve_run_refresh";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const std::filesystem::path proto_path = dir / "aquery.pb";
  const std::filesystem::path out_path = dir / "compile_commands.json";
  {
    std::ofstream proto_file(proto_path, std::ios::binary);
    const std::string bytes = container.SerializeAsString();
    proto_file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }

  ASSERT_THAT(
      RunRefresh(
          FileOptions{
              .aquery_proto_path = proto_path.string(),
              .output_path = out_path.string(),
              .directory = "/execroot/ws",
          }),
      IsOk());

  std::ifstream out(out_path, std::ios::binary);
  const std::string cdb_json((std::istreambuf_iterator<char>(out)), std::istreambuf_iterator<char>());
  EXPECT_THAT(cdb_json, HasSubstr("\"file\": \"/execroot/ws/src/a.cc\""));
  EXPECT_THAT(cdb_json, HasSubstr("\"directory\": \"/execroot/ws\""));
}

TEST(RunRefreshTest, MissingProtoFileIsNotFound) {
  EXPECT_THAT(
      RunRefresh(
          FileOptions{
              .aquery_proto_path = "/no/such/file.pb",
              .output_path = (std::filesystem::path(::testing::TempDir()) / "unused.json").string(),
              .directory = "/ws",
              .sidecar_path = "",
          }),
      StatusIs(absl::StatusCode::kNotFound));
}

// Returns FileOptions rooted at a fresh temporary directory `name`.
FileOptions TempRefresh(std::string_view name, const analysis::ActionGraphContainer& container) {
  const std::filesystem::path dir = std::filesystem::path(::testing::TempDir()) / name;
  std::filesystem::remove_all(dir);
  return FileOptions{
      .aquery_proto_path = WriteProto(dir / "aquery.pb", container).string(),
      .output_path = (dir / "compile_commands.json").string(),
      .directory = "/execroot/ws",
      .sidecar_path = (dir / "entries.binpb").string(),
  };
}

TEST(RunRefreshTest, UnchangedActionReusesStoredRecordWithCachedHeaders) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  for (std::string_view arg : {"clang", "-c", "src/a.cc"}) {
    compile->add_arguments(std::string(arg));
  }
  const FileOptions options = TempRefresh("carve_incremental_reuse", container);

  // Seed the sidecar with a record whose command matches the action's
  // post-de-Bazel argv, plus a cached header the fresh record will not have.
  ActionRecords seed;
  ActionRecord* seeded = seed.add_records();
  seeded->set_action_key("k1");
  seeded->add_sources("src/a.cc");
  for (std::string_view arg : {"clang", "-c", "src/a.cc"}) {
    seeded->add_command(std::string(arg));
  }
  seeded->add_headers("cached.h");
  ASSERT_THAT(sidecar::Save(options.sidecar_path, seed), IsOk());

  ASSERT_THAT(RunRefresh(options), IsOk());

  // The unchanged action keeps the cached record verbatim.
  EXPECT_THAT(
      sidecar::Load(options.sidecar_path),
      IsOkAndHolds(Property(&ActionRecords::SerializeAsString, Eq(seed.SerializeAsString()))));
}

TEST(RunRefreshTest, ChangedCommandRebuildsRecordDroppingStaleCache) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  for (std::string_view arg : {"clang", "-c", "src/a.cc"}) {
    compile->add_arguments(std::string(arg));
  }
  const FileOptions options = TempRefresh("carve_incremental_changed", container);

  // Seed a record under the same key but a different command, with a stale
  // cached header that must be discarded.
  ActionRecords seed;
  ActionRecord* seeded = seed.add_records();
  seeded->set_action_key("k1");
  seeded->add_command("clang");
  seeded->add_command("old");
  seeded->add_headers("stale.h");
  ASSERT_THAT(sidecar::Save(options.sidecar_path, seed), IsOk());

  ASSERT_THAT(RunRefresh(options), IsOk());

  // The record is rebuilt from the current action (no stale header).
  ActionRecords expected;
  ActionRecord* rebuilt = expected.add_records();
  rebuilt->set_action_key("k1");
  rebuilt->add_sources("src/a.cc");
  for (std::string_view arg : {"clang", "-c", "src/a.cc"}) {
    rebuilt->add_command(std::string(arg));
  }
  EXPECT_THAT(
      sidecar::Load(options.sidecar_path),
      IsOkAndHolds(Property(&ActionRecords::SerializeAsString, Eq(expected.SerializeAsString()))));
}

TEST(RunRefreshTest, PopulatesHeadersFromTheInjectedScanner) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  for (std::string_view arg : {"clang", "-c", "src/a.cc"}) {
    compile->add_arguments(std::string(arg));
  }
  FileOptions options = TempRefresh("carve_scan_headers", container);
  options.scanner = [](absl::Span<const std::string> /*argv*/,
                       std::string_view /*directory*/) -> absl::StatusOr<std::vector<std::string>> {
    return std::vector<std::string>{"src/a.cc", "dep.h"};
  };

  ASSERT_THAT(RunRefresh(options), IsOk());

  // The scanner's dependency paths are stored on the record as `headers`.
  EXPECT_THAT(sidecar::Load(options.sidecar_path), IsOkAndHolds(EqualsProto(R"pb(records {
                                                                                   action_key: "k1"
                                                                                   sources: "src/a.cc"
                                                                                   headers: "src/a.cc"
                                                                                   headers: "dep.h"
                                                                                   command: "clang"
                                                                                   command: "-c"
                                                                                   command: "src/a.cc"
                                                                                 })pb")));
}

TEST(RunRefreshTest, UnchangedActionIsNotRescanned) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  for (std::string_view arg : {"clang", "-c", "src/a.cc"}) {
    compile->add_arguments(std::string(arg));
  }
  FileOptions options = TempRefresh("carve_scan_skip", container);

  // Seed a matching record (same key+command) with a cached header. It is
  // stamped (written_at set) and its cached paths do not exist on disk, so the
  // staleness check cannot prove a change and the action is reused.
  const ActionRecords seed = ParseTextProtoOrDie(
      R"pb(records {
             action_key: "k1"
             sources: "src/a.cc"
             command: "clang"
             command: "-c"
             command: "src/a.cc"
             headers: "cached.h"
             written_at: 1
           })pb");
  ASSERT_THAT(sidecar::Save(options.sidecar_path, seed), IsOk());

  int scans = 0;
  options.scanner = [&scans](
                        absl::Span<const std::string> /*argv*/,
                        std::string_view /*directory*/) -> absl::StatusOr<std::vector<std::string>> {
    ++scans;
    return std::vector<std::string>{"FRESH.h"};
  };

  ASSERT_THAT(RunRefresh(options), IsOk());

  // The unchanged action was not re-scanned, and its cached header is preserved
  // (not replaced by the scanner's "FRESH.h").
  EXPECT_THAT(scans, Eq(0));
  EXPECT_THAT(sidecar::Load(options.sidecar_path), IsOkAndHolds(EqualsProto(seed)));
}

TEST(RunRefreshTest, StampsWrittenAtUsingTheInjectedClock) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  for (std::string_view arg : {"clang", "-c", "src/a.cc"}) {
    compile->add_arguments(std::string(arg));
  }
  FileOptions options = TempRefresh("carve_written_at", container);
  options.now = [] { return std::int64_t{1'700'000'000}; };

  ASSERT_THAT(RunRefresh(options), IsOk());

  // The freshly built record carries the injected `now`'s timestamp.
  EXPECT_THAT(sidecar::Load(options.sidecar_path), IsOkAndHolds(EqualsProto(R"pb(records {
                                                                                   action_key: "k1"
                                                                                   sources: "src/a.cc"
                                                                                   command: "clang"
                                                                                   command: "-c"
                                                                                   command: "src/a.cc"
                                                                                   written_at: 1700000000
                                                                                 })pb")));
}

TEST(RunRefreshTest, ReusedRecordIsRestampedKeepingCachedHeaders) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  for (std::string_view arg : {"clang", "-c", "src/a.cc"}) {
    compile->add_arguments(std::string(arg));
  }
  FileOptions options = TempRefresh("carve_written_at_reuse", container);
  options.now = [] { return std::int64_t{222}; };

  // Seed a matching record stamped at an older time, with a cached header.
  const ActionRecords seed = ParseTextProtoOrDie(
      R"pb(records {
             action_key: "k1"
             sources: "src/a.cc"
             command: "clang"
             command: "-c"
             command: "src/a.cc"
             headers: "cached.h"
             written_at: 111
           })pb");
  ASSERT_THAT(sidecar::Save(options.sidecar_path, seed), IsOk());

  ASSERT_THAT(RunRefresh(options), IsOk());

  // The reused record keeps its cached header (content unchanged) but is
  // restamped to the current time so prune sees the project as live.
  EXPECT_THAT(sidecar::Load(options.sidecar_path), IsOkAndHolds(EqualsProto(R"pb(records {
                                                                                   action_key: "k1"
                                                                                   sources: "src/a.cc"
                                                                                   command: "clang"
                                                                                   command: "-c"
                                                                                   command: "src/a.cc"
                                                                                   headers: "cached.h"
                                                                                   written_at: 222
                                                                                 })pb")));
}

TEST(RunRefreshTest, WritesHeaderIndexAlongsideTheSidecar) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  for (std::string_view arg : {"clang", "-c", "src/a.cc"}) {
    compile->add_arguments(std::string(arg));
  }
  FileOptions options = TempRefresh("carve_header_index", container);
  options.scanner = [](absl::Span<const std::string> /*argv*/,
                       std::string_view /*directory*/) -> absl::StatusOr<std::vector<std::string>> {
    return std::vector<std::string>{"src/a.cc", "dep.h"};
  };

  ASSERT_THAT(RunRefresh(options), IsOk());

  // The header index lands next to the sidecar and maps each scanned header to
  // its owning action (owners sorted by header_path).
  const std::filesystem::path index_path =
      std::filesystem::path(options.sidecar_path).parent_path() / "headers-index.binpb";
  EXPECT_THAT(
      sidecar::LoadHeaderIndex(index_path),
      IsOkAndHolds(EqualsProto(R"pb(owners { header_path: "dep.h" action_keys: "k1" }
                                    owners { header_path: "src/a.cc" action_keys: "k1" }
                                    schema_version: 1)pb")));
}

// Builds a refresh over a single `clang -c a.cc` action whose source and header
// exist on disk under `dir`, seeded with a matching sidecar record stamped at
// `written_at`. The scanner counts its calls and returns {header, new.h}.
struct StalenessFixture {
  std::filesystem::path dir;
  std::filesystem::path header;
  FileOptions options;
  int scans = 0;
};

StalenessFixture MakeStalenessFixture(std::string_view name, std::int64_t written_at) {
  StalenessFixture fixture;
  fixture.dir = std::filesystem::path(::testing::TempDir()) / name;
  std::filesystem::remove_all(fixture.dir);
  fixture.header = WriteText(fixture.dir / "dep.h", "// dep\n");
  WriteText(fixture.dir / "a.cc", "#include \"dep.h\"\n");

  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  for (std::string_view arg : {"clang", "-c", "a.cc"}) {
    compile->add_arguments(std::string(arg));
  }
  fixture.options = FileOptions{
      .aquery_proto_path = WriteProto(fixture.dir / "aquery.pb", container).string(),
      .output_path = (fixture.dir / "compile_commands.json").string(),
      .directory = fixture.dir.string(),
      .sidecar_path = (fixture.dir / "entries.binpb").string(),
  };

  // The stored record matches the action's key+command and caches `dep.h`; its
  // freshness is governed entirely by `written_at` vs the files' mtimes.
  const ActionRecords seed = ParseTextProtoOrDie(
      absl::Substitute(
          R"pb(records {
                 action_key: "k1"
                 sources: "a.cc"
                 command: "clang"
                 command: "-c"
                 command: "a.cc"
                 headers: "$0"
                 written_at: $1
               })pb",
          fixture.header.string(), written_at));
  ABSL_CHECK_OK(sidecar::Save(fixture.options.sidecar_path, seed));
  return fixture;
}

TEST(RunRefreshTest, EditedHeaderForcesRescanOfTheOwningAction) {
  // written_at = 1 (epoch): the on-disk source/header are far newer, so the
  // cached scan reads as stale and the action must be re-scanned.
  StalenessFixture fixture = MakeStalenessFixture("carve_stale_header", /*written_at=*/1);
  const std::string new_header = (fixture.dir / "new.h").string();
  fixture.options.scanner = [&fixture, &new_header](
                                absl::Span<const std::string> /*argv*/,
                                std::string_view /*directory*/) -> absl::StatusOr<std::vector<std::string>> {
    ++fixture.scans;
    return std::vector<std::string>{fixture.header.string(), new_header};
  };

  ASSERT_THAT(RunRefresh(fixture.options), IsOk());

  // The stale action was re-scanned exactly once and the fresh result (now
  // including new.h) replaced the cache.
  EXPECT_THAT(fixture.scans, Eq(1));
  EXPECT_THAT(
      sidecar::Load(fixture.options.sidecar_path), IsOkAndHolds(EqualsProto(
                                                       absl::Substitute(
                                                           R"pb(records {
                                                                  action_key: "k1"
                                                                  sources: "a.cc"
                                                                  command: "clang"
                                                                  command: "-c"
                                                                  command: "a.cc"
                                                                  headers: "$0"
                                                                  headers: "$1"
                                                                })pb",
                                                           fixture.header.string(), new_header))));
}

TEST(RunRefreshTest, UnmodifiedCachedScanIsReusedNotRescanned) {
  // written_at far in the future: the files' mtimes precede the recorded scan,
  // so the cache is fresh and the action is reused without scanning.
  StalenessFixture fixture = MakeStalenessFixture("carve_fresh_header", /*written_at=*/9'999'999'999);
  fixture.options.scanner = [&fixture](
                                absl::Span<const std::string> /*argv*/,
                                std::string_view /*directory*/) -> absl::StatusOr<std::vector<std::string>> {
    ++fixture.scans;
    return std::vector<std::string>{"UNEXPECTED.h"};
  };

  ASSERT_THAT(RunRefresh(fixture.options), IsOk());

  // Not re-scanned; the stored record (cached header + timestamp) is reused
  // verbatim.
  EXPECT_THAT(fixture.scans, Eq(0));
  EXPECT_THAT(
      sidecar::Load(fixture.options.sidecar_path), IsOkAndHolds(EqualsProto(
                                                       absl::Substitute(
                                                           R"pb(records {
                                                                  action_key: "k1"
                                                                  sources: "a.cc"
                                                                  command: "clang"
                                                                  command: "-c"
                                                                  command: "a.cc"
                                                                  headers: "$0"
                                                                  written_at: 9999999999
                                                                })pb",
                                                           fixture.header.string()))));
}

TEST(RunRefreshTest, FailedScanIsLeftUnstampedAndCounted) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  for (std::string_view arg : {"clang", "-c", "src/a.cc"}) {
    compile->add_arguments(std::string(arg));
  }
  FileOptions options = TempRefresh("carve_unresolved", container);
  options.now = [] { return std::int64_t{555}; };
  // Mimic scan-deps hitting an unbuilt generated header: the scan fails.
  options.scanner = [](absl::Span<const std::string> /*argv*/,
                       std::string_view /*directory*/) -> absl::StatusOr<std::vector<std::string>> {
    return absl::InvalidArgumentError("scan-deps failed: 'gen/foo.h' file not found");
  };

  // The failure is counted, and the record is stored WITHOUT headers and WITHOUT
  // a timestamp -- even though `now` is set -- so the next refresh re-scans
  // it instead of caching the incomplete result (CARVE_DESIGN.md section 4.2).
  EXPECT_THAT(RunRefresh(options), IsOkAndHolds(Field(&RefreshStats::unresolved, Eq(1))));
  EXPECT_THAT(sidecar::Load(options.sidecar_path), IsOkAndHolds(EqualsProto(R"pb(records {
                                                                                   action_key: "k1"
                                                                                   sources: "src/a.cc"
                                                                                   command: "clang"
                                                                                   command: "-c"
                                                                                   command: "src/a.cc"
                                                                                 })pb")));
}

TEST(RunRefreshTest, FailedScanIsRetriedOnTheNextRefresh) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  for (std::string_view arg : {"clang", "-c", "src/a.cc"}) {
    compile->add_arguments(std::string(arg));
  }
  FileOptions options = TempRefresh("carve_unresolved_retry", container);
  options.now = [] { return std::int64_t{555}; };
  bool scan_fails = true;
  options.scanner = [&scan_fails](
                        absl::Span<const std::string> /*argv*/,
                        std::string_view /*directory*/) -> absl::StatusOr<std::vector<std::string>> {
    if (scan_fails) {
      return absl::InvalidArgumentError("scan-deps failed: 'gen/foo.h' file not found");
    }
    return std::vector<std::string>{"resolved.h"};
  };

  // First refresh fails to resolve; the unstamped record is then re-scanned on
  // the second refresh (now that the generated header exists) and cached.
  ASSERT_THAT(RunRefresh(options), IsOkAndHolds(Field(&RefreshStats::unresolved, Eq(1))));
  scan_fails = false;
  EXPECT_THAT(RunRefresh(options), IsOkAndHolds(Field(&RefreshStats::scanned, Eq(1))));
  EXPECT_THAT(sidecar::Load(options.sidecar_path), IsOkAndHolds(EqualsProto(R"pb(records {
                                                                                   action_key: "k1"
                                                                                   sources: "src/a.cc"
                                                                                   command: "clang"
                                                                                   command: "-c"
                                                                                   command: "src/a.cc"
                                                                                   headers: "resolved.h"
                                                                                   written_at: 555
                                                                                 })pb")));
}

TEST(RunRefreshTest, ScansActionsInParallel) {
  // Four actions scanned with jobs=4. The scanner derives each action's header
  // from its own source operand (no shared state), so it is safe to call
  // concurrently and the result is deterministic regardless of thread
  // interleaving. Run under tsan in CI to exercise the worker pool.
  analysis::ActionGraphContainer container;
  for (std::string_view key : {"k1", "k2", "k3", "k4"}) {
    analysis::Action* compile = AddCompile(container, key);
    compile->add_arguments("clang");
    compile->add_arguments("-c");
    compile->add_arguments(absl::StrCat(key, ".cc"));
  }
  FileOptions options = TempRefresh("carve_parallel", container);
  options.jobs = 4;
  options.scanner = [](absl::Span<const std::string> argv,
                       std::string_view /*directory*/) -> absl::StatusOr<std::vector<std::string>> {
    return std::vector<std::string>{absl::StrCat(argv.back(), ".h")};  // e.g. "k1.cc" -> "k1.cc.h"
  };

  ASSERT_THAT(RunRefresh(options), IsOkAndHolds(Field(&RefreshStats::scanned, Eq(4))));
  EXPECT_THAT(
      sidecar::Load(options.sidecar_path), IsOkAndHolds(EqualsProto(
                                               R"pb(records {
                                                      action_key: "k1"
                                                      sources: "k1.cc"
                                                      command: "clang"
                                                      command: "-c"
                                                      command: "k1.cc"
                                                      headers: "k1.cc.h"
                                                    }
                                                    records {
                                                      action_key: "k2"
                                                      sources: "k2.cc"
                                                      command: "clang"
                                                      command: "-c"
                                                      command: "k2.cc"
                                                      headers: "k2.cc.h"
                                                    }
                                                    records {
                                                      action_key: "k3"
                                                      sources: "k3.cc"
                                                      command: "clang"
                                                      command: "-c"
                                                      command: "k3.cc"
                                                      headers: "k3.cc.h"
                                                    }
                                                    records {
                                                      action_key: "k4"
                                                      sources: "k4.cc"
                                                      command: "clang"
                                                      command: "-c"
                                                      command: "k4.cc"
                                                      headers: "k4.cc.h"
                                                    })pb")));
}

TEST(RunRefreshTest, ResolvesXcodePlaceholdersViaTheInjectedResolver) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  for (std::string_view arg :
       {"__BAZEL_XCODE_DEVELOPER_DIR__/usr/bin/clang", "-isysroot__BAZEL_XCODE_SDKROOT__", "-c", "a.cc"}) {
    compile->add_arguments(std::string(arg));
  }
  FileOptions options = TempRefresh("carve_xcode", container);
  options.xcode_resolver = [] { return XcodePaths{.developer_dir = "/Dev", .sdkroot = "/SDKs/MacOSX.sdk"}; };

  ASSERT_THAT(RunRefresh(options), IsOk());

  // The wrapped_clang placeholders are replaced with the resolver's paths.
  EXPECT_THAT(sidecar::Load(options.sidecar_path), IsOkAndHolds(EqualsProto(R"pb(records {
                                                                                   action_key: "k1"
                                                                                   sources: "a.cc"
                                                                                   command: "/Dev/usr/bin/clang"
                                                                                   command: "-isysroot/SDKs/MacOSX.sdk"
                                                                                   command: "-c"
                                                                                   command: "a.cc"
                                                                                 })pb")));
}

}  // namespace
}  // namespace carve::refresh
