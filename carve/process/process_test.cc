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

#include "carve/process/process.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace carve::process {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Field;
using ::testing::SizeIs;

TEST(RunTest, CapturesStdoutAndZeroExit) {
  EXPECT_THAT(
      ::carve::process::Run(std::vector<std::string>{"/bin/echo", "hello"}),
      IsOkAndHolds(AllOf(Field(&CommandResult::exit_code, Eq(0)), Field(&CommandResult::stdout_data, Eq("hello\n")))));
}

TEST(RunTest, CapturesStderrAndNonZeroExitSeparately) {
  EXPECT_THAT(
      ::carve::process::Run(std::vector<std::string>{"/bin/sh", "-c", "printf oops >&2; exit 3"}),
      IsOkAndHolds(AllOf(
          Field(&CommandResult::exit_code, Eq(3)), Field(&CommandResult::stdout_data, Eq("")),
          Field(&CommandResult::stderr_data, Eq("oops")))));
}

TEST(RunTest, MissingProgramReportsExit127) {
  EXPECT_THAT(
      ::carve::process::Run(std::vector<std::string>{"/no/such/program/carve"}),
      IsOkAndHolds(Field(&CommandResult::exit_code, Eq(127))));
}

TEST(RunTest, LargeOutputDoesNotDeadlock) {
  // 200000 'x' bytes exceeds a pipe buffer, so concurrent draining is required.
  EXPECT_THAT(
      ::carve::process::Run(std::vector<std::string>{"/bin/sh", "-c", "yes x | head -c 200000"}),
      IsOkAndHolds(
          AllOf(Field(&CommandResult::exit_code, Eq(0)), Field(&CommandResult::stdout_data, SizeIs(200'000)))));
}

TEST(RunTest, EmptyArgvIsInvalidArgument) {
  EXPECT_THAT(::carve::process::Run(std::vector<std::string>{}), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace carve::process
