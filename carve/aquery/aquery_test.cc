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

#include "absl/status/status_matchers.h"
#include "carve/third_party/bazel/analysis_v2.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace carve::aquery {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;

TEST(ParseCompileActionsTest, EmptyInputYieldsNoActions) {
  EXPECT_THAT(ParseCompileActions(""), IsOkAndHolds(IsEmpty()));
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

  EXPECT_THAT(
      ParseCompileActions(container.SerializeAsString()),
      IsOkAndHolds(ElementsAre(AllOf(
          Field(&CompileAction::action_key, Eq("k1")), Field(&CompileAction::mnemonic, Eq("CppCompile")),
          Field(&CompileAction::arguments, ElementsAre("clang", "-c", "src/main.cc")),
          Field(&CompileAction::primary_output, Eq("src/main.o"))))));
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

  EXPECT_THAT(
      ParseCompileActions(container.SerializeAsString()),
      IsOkAndHolds(ElementsAre(Field(&CompileAction::arguments, ElementsAre("clang", "-c", "src/a.cc", "-o", "a.o")))));
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

  EXPECT_THAT(
      ParseCompileActions(container.SerializeAsString()),
      IsOkAndHolds(ElementsAre(Field(&CompileAction::arguments, ElementsAre("clang", "@bazel-out/k1.params")))));
}

TEST(ParseCompileActionsTest, UnknownPrimaryOutputLeavesPathEmpty) {
  analysis::ActionGraphContainer container;
  analysis::Action* compile = container.add_actions();
  compile->set_mnemonic("ObjcCompile");
  compile->set_action_key("k2");
  compile->set_primary_output_id(999);  // No matching artifact.

  EXPECT_THAT(
      ParseCompileActions(container.SerializeAsString()),
      IsOkAndHolds(ElementsAre(Field(&CompileAction::primary_output, IsEmpty()))));
}

}  // namespace
}  // namespace carve::aquery
