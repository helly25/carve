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

#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "carve/cdb/cdb.h"
#include "carve/third_party/bazel/analysis_v2.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace carve::refresh {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

analysis::Action* AddCompile(analysis::ActionGraphContainer& container, std::string_view key) {
  analysis::Action* action = container.add_actions();
  action->set_mnemonic("CppCompile");
  action->set_action_key(std::string(key));
  return action;
}

TEST(BuildEntriesTest, MapsCompileActionToDeBazeledEntry) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  for (std::string_view arg :
       {"clang", "-fno-canonical-system-headers", "-c", "src/a.cc", "-o", "bazel-out/a.o"}) {
    compile->add_arguments(std::string(arg));
  }

  // `file` is made absolute against the directory; arguments stay exec-relative
  // and the de-Bazel transform dropped -fno-canonical-system-headers.
  EXPECT_THAT(
      BuildEntries(container.SerializeAsString(), Options{.directory = "/execroot/ws"}),
      IsOkAndHolds(ElementsAre(AllOf(
          Field(&cdb::CompileCommand::directory, Eq("/execroot/ws")),
          Field(&cdb::CompileCommand::file, Eq("/execroot/ws/src/a.cc")),
          Field(&cdb::CompileCommand::arguments,
                ElementsAre("clang", "-c", "src/a.cc", "-o", "bazel-out/a.o"))))));
}

TEST(BuildEntriesTest, AbsoluteSourcePathIsLeftUnchanged) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  compile->add_arguments("clang");
  compile->add_arguments("-c");
  compile->add_arguments("/abs/src/a.cc");

  EXPECT_THAT(BuildEntries(container.SerializeAsString(), Options{.directory = "/execroot/ws"}),
              IsOkAndHolds(ElementsAre(Field(&cdb::CompileCommand::file, Eq("/abs/src/a.cc")))));
}

TEST(BuildEntriesTest, EmptyDirectoryLeavesSourceRelative) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  compile->add_arguments("clang");
  compile->add_arguments("-c");
  compile->add_arguments("src/a.cc");

  EXPECT_THAT(BuildEntries(container.SerializeAsString(), Options{.directory = ""}),
              IsOkAndHolds(ElementsAre(Field(&cdb::CompileCommand::file, Eq("src/a.cc")))));
}

TEST(BuildEntriesTest, SkipsActionsWithoutADetectableSource) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  compile->add_arguments("clang");
  compile->add_arguments("--version");  // No source operand.

  EXPECT_THAT(BuildEntries(container.SerializeAsString(), Options{.directory = "/ws"}),
              IsOkAndHolds(IsEmpty()));
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

  ASSERT_THAT(RunRefresh(FileOptions{
                  .aquery_proto_path = proto_path.string(),
                  .output_path = out_path.string(),
                  .directory = "/execroot/ws",
              }),
              IsOk());

  std::ifstream out(out_path, std::ios::binary);
  const std::string cdb_json((std::istreambuf_iterator<char>(out)),
                             std::istreambuf_iterator<char>());
  EXPECT_THAT(cdb_json, HasSubstr("\"file\": \"/execroot/ws/src/a.cc\""));
  EXPECT_THAT(cdb_json, HasSubstr("\"directory\": \"/execroot/ws\""));
}

TEST(RunRefreshTest, MissingProtoFileIsNotFound) {
  EXPECT_THAT(RunRefresh(FileOptions{
                  .aquery_proto_path = "/no/such/file.pb",
                  .output_path = (std::filesystem::path(::testing::TempDir()) / "unused.json").string(),
                  .directory = "/ws",
              }),
              StatusIs(absl::StatusCode::kNotFound));
}

}  // namespace
}  // namespace carve::refresh
