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

#include "carve/aquery/aquery.h"

#include "absl/status/statusor.h"
#include "carve/third_party/bazel/analysis_v2.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace carve::aquery {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

TEST(ParseCompileActionsTest, EmptyInputYieldsNoActions) {
  const absl::StatusOr<std::vector<CompileAction>> actions = ParseCompileActions("");
  ASSERT_TRUE(actions.ok());
  EXPECT_THAT(*actions, IsEmpty());
}

TEST(ParseCompileActionsTest, ExtractsCompileActionAndResolvesOutputPath) {
  analysis::ActionGraphContainer container;
  // Path fragments forming "src/main.o".
  analysis::PathFragment* src = container.add_path_fragments();
  src->set_id(1);
  src->set_label("src");
  src->set_parent_id(0);
  analysis::PathFragment* obj = container.add_path_fragments();
  obj->set_id(2);
  obj->set_label("main.o");
  obj->set_parent_id(1);

  analysis::Artifact* artifact = container.add_artifacts();
  artifact->set_id(10);
  artifact->set_path_fragment_id(2);

  analysis::Action* compile = container.add_actions();
  compile->set_mnemonic("CppCompile");
  compile->set_action_key("k1");
  compile->add_arguments("clang");
  compile->add_arguments("-c");
  compile->add_arguments("src/main.cc");
  compile->set_primary_output_id(10);

  // A non-compile action must be filtered out.
  analysis::Action* genrule = container.add_actions();
  genrule->set_mnemonic("Genrule");
  genrule->set_action_key("g1");

  const absl::StatusOr<std::vector<CompileAction>> actions =
      ParseCompileActions(container.SerializeAsString());
  ASSERT_TRUE(actions.ok());
  ASSERT_EQ(actions->size(), 1U);
  const CompileAction& got = actions->front();
  EXPECT_EQ(got.action_key, "k1");
  EXPECT_EQ(got.mnemonic, "CppCompile");
  EXPECT_THAT(got.arguments, ElementsAre("clang", "-c", "src/main.cc"));
  EXPECT_EQ(got.primary_output, "src/main.o");
}

TEST(ParseCompileActionsTest, ExpandsEmbeddedParamFile) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = container.add_actions();
  compile->set_mnemonic("CppCompile");
  compile->set_action_key("k1");
  compile->add_arguments("clang");
  compile->add_arguments("@bazel-out/k1.params");
  analysis::ParamFile* param_file = compile->add_param_files();
  param_file->set_exec_path("bazel-out/k1.params");
  param_file->add_arguments("-c");
  param_file->add_arguments("src/a.cc");
  param_file->add_arguments("-o");
  param_file->add_arguments("a.o");

  const absl::StatusOr<std::vector<CompileAction>> actions =
      ParseCompileActions(container.SerializeAsString());
  ASSERT_TRUE(actions.ok());
  ASSERT_EQ(actions->size(), 1U);
  EXPECT_THAT(actions->front().arguments, ElementsAre("clang", "-c", "src/a.cc", "-o", "a.o"));
}

TEST(ParseCompileActionsTest, UnmatchedResponseFileTokenIsKeptVerbatim) {
  // Models `bazel aquery` without --include_param_files: no embedded param
  // files, so the @-token cannot be expanded and must pass through.
  analysis::ActionGraphContainer container;
  analysis::Action* compile = container.add_actions();
  compile->set_mnemonic("CppCompile");
  compile->set_action_key("k1");
  compile->add_arguments("clang");
  compile->add_arguments("@bazel-out/k1.params");

  const absl::StatusOr<std::vector<CompileAction>> actions =
      ParseCompileActions(container.SerializeAsString());
  ASSERT_TRUE(actions.ok());
  ASSERT_EQ(actions->size(), 1U);
  EXPECT_THAT(actions->front().arguments, ElementsAre("clang", "@bazel-out/k1.params"));
}

TEST(ParseCompileActionsTest, UnknownPrimaryOutputLeavesPathEmpty) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = container.add_actions();
  compile->set_mnemonic("ObjcCompile");
  compile->set_action_key("k2");
  compile->set_primary_output_id(999);  // No matching artifact.

  const absl::StatusOr<std::vector<CompileAction>> actions =
      ParseCompileActions(container.SerializeAsString());
  ASSERT_TRUE(actions.ok());
  ASSERT_EQ(actions->size(), 1U);
  EXPECT_EQ(actions->front().primary_output, "");
}

}  // namespace
}  // namespace carve::aquery
