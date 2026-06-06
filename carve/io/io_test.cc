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

#include "carve/io/io.h"

#include <filesystem>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace carve::io {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::SizeIs;

TEST(WriteAtomicallyTest, CreatesParentsAndWritesContent) {
  const std::filesystem::path path =
      std::filesystem::path(::testing::TempDir()) / "carve_io_write" / "nested" / "out.bin";
  std::filesystem::remove_all(std::filesystem::path(::testing::TempDir()) / "carve_io_write");

  ASSERT_THAT(WriteAtomically(path, "hello\n"), IsOk());
  EXPECT_THAT(ReadFile(path), IsOkAndHolds(Eq("hello\n")));
}

TEST(WriteAtomicallyTest, OverwritesInPlaceWithoutLeavingTempFiles) {
  const std::filesystem::path dir = std::filesystem::path(::testing::TempDir()) / "carve_io_overwrite";
  std::filesystem::remove_all(dir);
  const std::filesystem::path path = dir / "out.bin";

  ASSERT_THAT(WriteAtomically(path, "first"), IsOk());
  ASSERT_THAT(WriteAtomically(path, "second"), IsOk());
  EXPECT_THAT(ReadFile(path), IsOkAndHolds(Eq("second")));

  std::vector<std::filesystem::path> entries;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    entries.push_back(entry.path());
  }
  EXPECT_THAT(entries, SizeIs(1)) << "temp files were left behind";
}

TEST(ReadFileTest, MissingFileIsNotFound) {
  EXPECT_THAT(ReadFile("/no/such/carve/file"), StatusIs(absl::StatusCode::kNotFound));
}

}  // namespace
}  // namespace carve::io
