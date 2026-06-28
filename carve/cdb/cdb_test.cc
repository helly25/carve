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

#include "carve/cdb/cdb.h"

#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <vector>

#include "carve/cdb/json_matcher.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace carve::cdb {
namespace {

using ::mbo::testing::IsOk;
using ::testing::Eq;
using ::testing::HasSubstr;

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

TEST(ToJsonTest, EmptyIsBracketsAndNewline) {
  EXPECT_THAT(ToJson({}), Eq("[]\n"));
}

TEST(ToJsonTest, SingleEntryWithArguments) {
  const std::vector<CompileCommand> entries = {
      {.directory = "/work", .file = "a.cc", .arguments = {"clang", "-c", "a.cc"}, .output = ""},
  };
  EXPECT_THAT(  // NL
      ToJson(entries),
      EqJson(R"json([{"directory": "/work", "file": "a.cc", "arguments": ["clang", "-c", "a.cc"]}])json"));
}

TEST(ToJsonTest, OutputFieldEmittedWhenPresent) {
  const std::vector<CompileCommand> entries = {
      {.directory = "/w", .file = "a.cc", .arguments = {"cc"}, .output = "a.o"},
  };
  EXPECT_THAT(ToJson(entries), HasSubstr("\"output\": \"a.o\""));
}

TEST(ToJsonTest, EscapesSpecialCharacters) {
  const std::vector<CompileCommand> entries = {
      {.directory = "/w", .file = std::string("a\"b\\c\nd\te"), .arguments = {}, .output = ""},
  };
  EXPECT_THAT(ToJson(entries), HasSubstr("\"file\": \"a\\\"b\\\\c\\nd\\te\""));
}

TEST(ToJsonTest, ControlCharacterUsesUnicodeEscape) {
  const std::vector<CompileCommand> entries = {
      {.directory = "/w", .file = std::string(1, '\x01'), .arguments = {}, .output = ""},
  };
  EXPECT_THAT(ToJson(entries), HasSubstr("\\u0001"));
}

TEST(ToJsonTest, EntriesAreCommaSeparated) {
  const std::vector<CompileCommand> entries = {
      {.directory = "/w", .file = "a.cc", .arguments = {}, .output = ""},
      {.directory = "/w", .file = "b.cc", .arguments = {}, .output = ""},
  };
  EXPECT_THAT(  // NL
      ToJson(entries),
      EqJson(R"json([{"directory": "/w", "file": "a.cc"}, {"directory": "/w", "file": "b.cc"}])json"));
}

TEST(WriteTest, RoundTripsThroughToJson) {
  const std::vector<CompileCommand> entries = {
      {.directory = "/w", .file = "a.cc", .arguments = {"cc", "-c", "a.cc"}, .output = ""},
  };
  const std::filesystem::path path = std::filesystem::path(::testing::TempDir()) / "carve_cdb_roundtrip.json";
  ASSERT_THAT(Write(path, entries), IsOk());
  EXPECT_THAT(ReadFile(path), Eq(ToJson(entries)));
}

}  // namespace
}  // namespace carve::cdb
