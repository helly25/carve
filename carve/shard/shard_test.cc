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

#include "carve/shard/shard.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "carve/io/io.h"
#include "carve/sidecar/carve.pb.h"
#include "carve/sidecar/sidecar.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/proto/matchers.h"
#include "mbo/proto/parse_text_proto.h"

namespace carve::shard {
namespace {

using ::absl_testing::IsOk;
using ::mbo::proto::EqualsProto;
using ::mbo::proto::ParseTextProtoOrDie;

// A scanner returning a fixed header set, regardless of input.
HeaderScanner FixedScanner(std::vector<std::string> headers) {
  return [headers = std::move(headers)](
             absl::Span<const std::string>, std::string_view) -> absl::StatusOr<std::vector<std::string>> {
    return headers;
  };
}

// A scanner that always fails (e.g. an unbuilt generated header).
HeaderScanner FailingScanner() {
  return [](absl::Span<const std::string>, std::string_view) -> absl::StatusOr<std::vector<std::string>> {
    return absl::NotFoundError("missing header");
  };
}

NowFn FixedNow(std::int64_t value) {
  return [value] { return value; };
}

TEST(BuildShardTest, DeBazelsScansAndStamps) {
  Options options;
  options.action_key = "k";
  options.command = {"clang", "-fno-canonical-system-headers", "-c", "a.cc"};  // the flag is dropped by DeBazel
  options.source = "a.cc";
  options.project_id = "p";
  options.primary_output = "bazel-out/a.o";
  options.directory = "/exec";
  options.scanner = FixedScanner({"a.cc", "h/one.h"});
  options.now = FixedNow(12'345);

  EXPECT_THAT(BuildShard(options), EqualsProto(R"pb(records {
                                                      action_key: "k"
                                                      sources: "a.cc"
                                                      headers: "a.cc"
                                                      headers: "h/one.h"
                                                      command: "clang"
                                                      command: "-c"
                                                      command: "a.cc"
                                                      project_id: "p"
                                                      written_at: 12345
                                                      primary_output: "bazel-out/a.o"
                                                    })pb"));
}

TEST(BuildShardTest, FailedScanRecordsNoHeadersAndLeavesUnstamped) {
  Options options;
  options.action_key = "k";
  options.command = {"clang", "-c", "a.cc"};
  options.source = "a.cc";
  options.scanner = FailingScanner();
  options.now = FixedNow(999);  // present, but the incomplete scan must NOT stamp

  EXPECT_THAT(
      BuildShard(options),
      EqualsProto(R"pb(records { action_key: "k" sources: "a.cc" command: "clang" command: "-c" command: "a.cc" })pb"));
}

TEST(BuildShardTest, NoScannerStampsWithoutHeaders) {
  Options options;
  options.action_key = "k";
  options.command = {"clang", "-c", "a.cc"};
  options.source = "a.cc";
  options.scanner = nullptr;  // header scanning disabled
  options.now = FixedNow(7);

  EXPECT_THAT(BuildShard(options), EqualsProto(R"pb(records {
                                                      action_key: "k"
                                                      sources: "a.cc"
                                                      command: "clang"
                                                      command: "-c"
                                                      command: "a.cc"
                                                      written_at: 7
                                                    })pb"));
}

TEST(BuildShardTest, ResolvesXcodePlaceholders) {
  Options options;
  options.action_key = "k";
  options.command = {"clang", "-isysroot__BAZEL_XCODE_SDKROOT__", "-c", "a.cc"};
  options.source = "a.cc";
  options.xcode_sdkroot = "/SDKs/MacOSX.sdk";
  // No scanner, no `now`: isolate the placeholder substitution.

  EXPECT_THAT(BuildShard(options), EqualsProto(R"pb(records {
                                                      action_key: "k"
                                                      sources: "a.cc"
                                                      command: "clang"
                                                      command: "-isysroot/SDKs/MacOSX.sdk"
                                                      command: "-c"
                                                      command: "a.cc"
                                                    })pb"));
}

TEST(RunShardTest, ReadsCommandFileAndWritesShard) {
  const std::filesystem::path dir = std::filesystem::path(::testing::TempDir()) / "carve_shard";
  std::filesystem::remove_all(dir);
  const std::filesystem::path command_file = dir / "command.txt";
  const std::filesystem::path out = dir / "shard.binpb";
  // Multiline param-file format with a trailing newline (skipped).
  ASSERT_THAT(io::WriteAtomically(command_file, "clang\n-c\na.cc\n"), IsOk());

  FileOptions options;
  options.action_key = "k";
  options.command_file = command_file.string();
  options.source = "a.cc";
  options.project_id = "p";
  options.scanner = FixedScanner({"a.cc"});
  options.now = FixedNow(42);
  options.out_path = out.string();

  ASSERT_THAT(RunShard(options), IsOk());
  EXPECT_THAT(sidecar::Load(out), ::absl_testing::IsOkAndHolds(EqualsProto(R"pb(records {
                                                                                  action_key: "k"
                                                                                  sources: "a.cc"
                                                                                  headers: "a.cc"
                                                                                  command: "clang"
                                                                                  command: "-c"
                                                                                  command: "a.cc"
                                                                                  project_id: "p"
                                                                                  written_at: 42
                                                                                })pb")));
}

TEST(RunShardTest, MissingCommandFileIsAnError) {
  FileOptions options;
  options.action_key = "k";
  options.command_file = "/no/such/command/file.txt";
  options.source = "a.cc";
  options.out_path = (std::filesystem::path(::testing::TempDir()) / "unused.binpb").string();

  EXPECT_THAT(RunShard(options), ::testing::Not(IsOk()));
}

}  // namespace
}  // namespace carve::shard
