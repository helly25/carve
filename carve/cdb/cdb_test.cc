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

#include "gtest/gtest.h"

namespace carve::cdb {
namespace {

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

TEST(ToJsonTest, EmptyIsBracketsAndNewline) {
  EXPECT_EQ(ToJson({}), "[]\n");
}

TEST(ToJsonTest, SingleEntryWithArguments) {
  const std::vector<CompileCommand> entries = {
      {.directory = "/work", .file = "a.cc", .arguments = {"clang", "-c", "a.cc"}, .output = ""},
  };
  EXPECT_EQ(ToJson(entries),
            "[\n"
            "  {\n"
            "    \"directory\": \"/work\",\n"
            "    \"file\": \"a.cc\",\n"
            "    \"arguments\": [\n"
            "      \"clang\",\n"
            "      \"-c\",\n"
            "      \"a.cc\"\n"
            "    ]\n"
            "  }\n"
            "]\n");
}

TEST(ToJsonTest, OutputFieldEmittedWhenPresent) {
  const std::vector<CompileCommand> entries = {
      {.directory = "/w", .file = "a.cc", .arguments = {"cc"}, .output = "a.o"},
  };
  const std::string json = ToJson(entries);
  EXPECT_NE(json.find("\"output\": \"a.o\""), std::string::npos);
}

TEST(ToJsonTest, EscapesSpecialCharacters) {
  const std::vector<CompileCommand> entries = {
      {.directory = "/w", .file = std::string("a\"b\\c\nd\te"), .arguments = {}, .output = ""},
  };
  const std::string json = ToJson(entries);
  EXPECT_NE(json.find("\"file\": \"a\\\"b\\\\c\\nd\\te\""), std::string::npos) << json;
}

TEST(ToJsonTest, ControlCharacterUsesUnicodeEscape) {
  const std::vector<CompileCommand> entries = {
      {.directory = "/w", .file = std::string(1, '\x01'), .arguments = {}, .output = ""},
  };
  EXPECT_NE(ToJson(entries).find("\\u0001"), std::string::npos);
}

TEST(ToJsonTest, EntriesAreCommaSeparated) {
  const std::vector<CompileCommand> entries = {
      {.directory = "/w", .file = "a.cc", .arguments = {}, .output = ""},
      {.directory = "/w", .file = "b.cc", .arguments = {}, .output = ""},
  };
  EXPECT_EQ(ToJson(entries),
            "[\n"
            "  {\n"
            "    \"directory\": \"/w\",\n"
            "    \"file\": \"a.cc\"\n"
            "  },\n"
            "  {\n"
            "    \"directory\": \"/w\",\n"
            "    \"file\": \"b.cc\"\n"
            "  }\n"
            "]\n");
}

TEST(WriteAtomicallyTest, WritesContentExactly) {
  const std::filesystem::path path =
      std::filesystem::path(::testing::TempDir()) / "carve_cdb_write" / "compile_commands.json";
  std::filesystem::remove_all(path.parent_path());

  ASSERT_TRUE(WriteAtomically(path, "hello\n").ok());
  EXPECT_EQ(ReadFile(path), "hello\n");

  // Overwrites in place, leaving no stray temp files behind.
  ASSERT_TRUE(WriteAtomically(path, "world").ok());
  EXPECT_EQ(ReadFile(path), "world");

  int entries = 0;
  for (const auto& dir_entry : std::filesystem::directory_iterator(path.parent_path())) {
    static_cast<void>(dir_entry);
    ++entries;
  }
  EXPECT_EQ(entries, 1) << "temp files were left behind";
}

TEST(WriteTest, RoundTripsThroughToJson) {
  const std::vector<CompileCommand> entries = {
      {.directory = "/w", .file = "a.cc", .arguments = {"cc", "-c", "a.cc"}, .output = ""},
  };
  const std::filesystem::path path =
      std::filesystem::path(::testing::TempDir()) / "carve_cdb_roundtrip.json";
  ASSERT_TRUE(Write(path, entries).ok());
  EXPECT_EQ(ReadFile(path), ToJson(entries));
}

}  // namespace
}  // namespace carve::cdb
