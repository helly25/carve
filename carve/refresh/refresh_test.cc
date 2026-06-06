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

#include "absl/status/statusor.h"
#include "carve/third_party/bazel/analysis_v2.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace carve::refresh {
namespace {

using ::testing::ElementsAre;
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

  const absl::StatusOr<std::vector<cdb::CompileCommand>> entries =
      BuildEntries(container.SerializeAsString(), Options{.directory = "/execroot/ws"});
  ASSERT_TRUE(entries.ok());
  ASSERT_EQ(entries->size(), 1U);
  const cdb::CompileCommand& got = entries->front();
  EXPECT_EQ(got.directory, "/execroot/ws");
  EXPECT_EQ(got.file, "src/a.cc");
  // The de-Bazel transform dropped -fno-canonical-system-headers.
  EXPECT_THAT(got.arguments, ElementsAre("clang", "-c", "src/a.cc", "-o", "bazel-out/a.o"));
}

TEST(BuildEntriesTest, SkipsActionsWithoutADetectableSource) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = AddCompile(container, "k1");
  compile->add_arguments("clang");
  compile->add_arguments("--version");  // No source operand.

  const absl::StatusOr<std::vector<cdb::CompileCommand>> entries =
      BuildEntries(container.SerializeAsString(), Options{.directory = "/ws"});
  ASSERT_TRUE(entries.ok());
  EXPECT_THAT(*entries, IsEmpty());
}

TEST(BuildEntriesTest, EmptyInputYieldsNoEntries) {
  const absl::StatusOr<std::vector<cdb::CompileCommand>> entries =
      BuildEntries("", Options{.directory = "/ws"});
  ASSERT_TRUE(entries.ok());
  EXPECT_THAT(*entries, IsEmpty());
}

}  // namespace
}  // namespace carve::refresh
