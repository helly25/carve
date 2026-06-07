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

// End-to-end tests that drive the real `carve` binary. The aquery action graph
// is supplied via --aquery_proto (a pre-serialized ActionGraphContainer) so the
// tests are hermetic and do not run a nested Bazel.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status_matchers.h"
#include "carve/process/process.h"
#include "carve/third_party/bazel/analysis_v2.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace carve {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::bazel::tools::cpp::runfiles::Runfiles;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;

// Absolute path to the built `carve` binary in the test's runfiles.
std::string CarveBinary() {
  std::string error;
  const std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&error));
  EXPECT_NE(runfiles, nullptr) << error;
  const char* workspace = std::getenv("TEST_WORKSPACE");
  return runfiles->Rlocation(std::string(workspace != nullptr ? workspace : "_main") +
                             "/carve/carve");
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::filesystem::path WriteAqueryProto(const std::filesystem::path& path) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = container.add_actions();
  compile->set_mnemonic("CppCompile");
  compile->set_action_key("k1");
  compile->add_arguments("clang");
  compile->add_arguments("-c");
  compile->add_arguments("src/a.cc");
  std::ofstream file(path, std::ios::binary);
  const std::string bytes = container.SerializeAsString();
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return path;
}

TEST(EndToEndTest, RefreshFromProtoWritesCompileCommands) {
  const std::filesystem::path dir = std::filesystem::path(::testing::TempDir()) / "carve_e2e";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const std::filesystem::path proto = WriteAqueryProto(dir / "aquery.pb");
  const std::filesystem::path out = dir / "compile_commands.json";

  ASSERT_THAT(process::Run({CarveBinary(), "refresh", "--aquery_proto=" + proto.string(),
                            "--output=" + out.string(), "--sidecar=", "--directory=/execroot/ws"}),
              IsOkAndHolds(Field(&process::CommandResult::exit_code, Eq(0))));

  const std::string cdb = ReadFile(out);
  EXPECT_THAT(cdb, HasSubstr("\"file\": \"/execroot/ws/src/a.cc\""));
  EXPECT_THAT(cdb, HasSubstr("\"directory\": \"/execroot/ws\""));
}

TEST(EndToEndTest, MissingSubcommandExitsTwo) {
  EXPECT_THAT(process::Run({CarveBinary()}),
              IsOkAndHolds(Field(&process::CommandResult::exit_code, Eq(2))));
}

TEST(EndToEndTest, UnknownSubcommandExitsTwo) {
  EXPECT_THAT(process::Run({CarveBinary(), "frobnicate"}),
              IsOkAndHolds(Field(&process::CommandResult::exit_code, Eq(2))));
}

TEST(EndToEndTest, MissingAqueryProtoExitsOne) {
  EXPECT_THAT(process::Run({CarveBinary(), "refresh", "--aquery_proto=/no/such/file.pb",
                            "--sidecar=", "--directory=/ws"}),
              IsOkAndHolds(Field(&process::CommandResult::exit_code, Eq(1))));
}

}  // namespace
}  // namespace carve
