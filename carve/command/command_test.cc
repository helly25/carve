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

#include "carve/command/command.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace carve::command {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

TEST(DeBazelTest, EmptyStaysEmpty) {
  EXPECT_THAT(DeBazel(std::vector<std::string>{}), IsEmpty());
}

TEST(DeBazelTest, PreservesUnaffectedArgsInOrder) {
  const std::vector<std::string> argv = {"clang", "-c", "a.cc", "-o", "a.o", "-I", "include"};
  EXPECT_THAT(DeBazel(argv), ElementsAre("clang", "-c", "a.cc", "-o", "a.o", "-I", "include"));
}

TEST(DeBazelTest, DropsNoCanonicalSystemHeaders) {
  const std::vector<std::string> argv = {"clang", "-fno-canonical-system-headers", "-c", "a.cc"};
  EXPECT_THAT(DeBazel(argv), ElementsAre("clang", "-c", "a.cc"));
}

TEST(DeBazelTest, DropsJoinedGccToolchain) {
  const std::vector<std::string> argv = {"clang", "--gcc-toolchain=/opt/gcc", "-c", "a.cc"};
  EXPECT_THAT(DeBazel(argv), ElementsAre("clang", "-c", "a.cc"));
}

TEST(DeBazelTest, DropsSeparatedGccToolchainAndItsValue) {
  const std::vector<std::string> argv = {"clang", "-gcc-toolchain", "/opt/gcc", "-c", "a.cc"};
  EXPECT_THAT(DeBazel(argv), ElementsAre("clang", "-c", "a.cc"));
}

TEST(DeBazelTest, DropsDoubleDashSeparatedGccToolchainAndItsValue) {
  const std::vector<std::string> argv = {"clang", "--gcc-toolchain", "/opt/gcc", "-c", "a.cc"};
  EXPECT_THAT(DeBazel(argv), ElementsAre("clang", "-c", "a.cc"));
}

TEST(DeBazelTest, TrailingValuelessFlagWithValueDoesNotConsumePastEnd) {
  const std::vector<std::string> argv = {"clang", "-gcc-toolchain"};
  EXPECT_THAT(DeBazel(argv), ElementsAre("clang"));
}

TEST(DeBazelTest, DropsMultipleQuirksTogether) {
  const std::vector<std::string> argv = {
      "clang", "-fno-canonical-system-headers", "--gcc-toolchain=/opt/gcc",
      "-c",    "a.cc",                          "-gcc-toolchain",
      "/x",    "-o",                            "a.o"};
  EXPECT_THAT(DeBazel(argv), ElementsAre("clang", "-c", "a.cc", "-o", "a.o"));
}

}  // namespace
}  // namespace carve::command
