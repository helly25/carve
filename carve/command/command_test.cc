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
      "clang", "-fno-canonical-system-headers", "--gcc-toolchain=/opt/gcc", "-c", "a.cc", "-gcc-toolchain", "/x", "-o",
      "a.o"};
  EXPECT_THAT(DeBazel(argv), ElementsAre("clang", "-c", "a.cc", "-o", "a.o"));
}

TEST(DeBazelTest, DropsLeadingCcacheWrapper) {
  EXPECT_THAT(DeBazel(std::vector<std::string>{"ccache", "clang", "-c", "a.cc"}), ElementsAre("clang", "-c", "a.cc"));
}

TEST(DeBazelTest, DropsLeadingCcacheWrapperWhenPathQualified) {
  EXPECT_THAT(
      DeBazel(std::vector<std::string>{"/usr/lib/ccache/bin/ccache", "g++", "-c", "a.cc"}),
      ElementsAre("g++", "-c", "a.cc"));
}

TEST(DeBazelTest, OnlyDropsCcacheInLeadPosition) {
  // A literal `ccache` token anywhere but argv[0] is not a wrapper; keep it.
  EXPECT_THAT(DeBazel(std::vector<std::string>{"clang", "ccache", "-c"}), ElementsAre("clang", "ccache", "-c"));
}

TEST(DeBazelTest, DropsMsvcShowIncludes) {
  EXPECT_THAT(
      DeBazel(std::vector<std::string>{"clang-cl", "/showIncludes", "/showIncludes:user", "-c", "a.cc"}),
      ElementsAre("clang-cl", "-c", "a.cc"));
}

TEST(DeBazelTest, DropsBazelOutModulesCachePath) {
  EXPECT_THAT(
      DeBazel(std::vector<std::string>{"clang", "-fmodules-cache-path=bazel-out/darwin/cache", "-c", "a.cc"}),
      ElementsAre("clang", "-c", "a.cc"));
}

TEST(DeBazelTest, KeepsNonBazelOutModulesCachePath) {
  // Only the per-build bazel-out cache is dropped; a user-set cache is kept.
  const std::vector<std::string> argv = {"clang", "-fmodules-cache-path=/home/me/.cache/clang", "-c", "a.cc"};
  EXPECT_THAT(DeBazel(argv), ElementsAre("clang", "-fmodules-cache-path=/home/me/.cache/clang", "-c", "a.cc"));
}

TEST(ResolveXcodePlaceholdersTest, SubstitutesDeveloperDirAndSdkrootSubstrings) {
  const std::vector<std::string> argv = {
      "__BAZEL_XCODE_DEVELOPER_DIR__/usr/bin/clang", "-isysroot__BAZEL_XCODE_SDKROOT__", "-c", "a.cc"};
  EXPECT_THAT(
      ResolveXcodePlaceholders(argv, "/Applications/Xcode.app/Contents/Developer", "/SDKs/MacOSX.sdk"),
      ElementsAre(
          "/Applications/Xcode.app/Contents/Developer/usr/bin/clang", "-isysroot/SDKs/MacOSX.sdk", "-c", "a.cc"));
}

TEST(ResolveXcodePlaceholdersTest, EmptyReplacementsLeavePlaceholdersUntouched) {
  const std::vector<std::string> argv = {"-isysroot__BAZEL_XCODE_SDKROOT__", "-c", "a.cc"};
  EXPECT_THAT(ResolveXcodePlaceholders(argv, "", ""), ElementsAre("-isysroot__BAZEL_XCODE_SDKROOT__", "-c", "a.cc"));
}

TEST(RelativizeToExecrootTest, StripsTheExecrootPrefix) {
  EXPECT_EQ(
      RelativizeToExecroot(
          "/cache/execroot/_main/bazel-out/k8-fastbuild/bin/external/x/h.inc", "/cache/execroot/_main"),
      "bazel-out/k8-fastbuild/bin/external/x/h.inc");
}

TEST(RelativizeToExecrootTest, LeavesAnAlreadyRelativePathUnchanged) {
  EXPECT_EQ(
      RelativizeToExecroot("bazel-out/k8-fastbuild/bin/x.h", "/cache/execroot/_main"),
      "bazel-out/k8-fastbuild/bin/x.h");
}

TEST(RelativizeToExecrootTest, LeavesAPathOutsideTheExecrootAbsolute) {
  // A genuine system header is not under the execroot and cannot be made relative.
  EXPECT_EQ(RelativizeToExecroot("/usr/include/stdio.h", "/cache/execroot/_main"), "/usr/include/stdio.h");
}

TEST(RelativizeToExecrootTest, DoesNotStripAMereStringPrefix) {
  // "/a/b_main" shares a string prefix with "/a/b" but is not a path child of it;
  // relativization is path-component-aware, so the path stays absolute.
  EXPECT_EQ(RelativizeToExecroot("/a/b_main/x.h", "/a/b"), "/a/b_main/x.h");
}

TEST(RelativizeToExecrootTest, EmptyExecrootIsANoOp) {
  EXPECT_EQ(RelativizeToExecroot("/cache/execroot/_main/x.h", ""), "/cache/execroot/_main/x.h");
}

}  // namespace
}  // namespace carve::command
