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

#include "carve/cdb/json_matcher.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace carve::cdb {
namespace {

using ::testing::HasSubstr;
using ::testing::Matcher;
using ::testing::Not;
using ::testing::StringMatchResultListener;

struct JsonMatcherTest : ::testing::Test {};

// Runs `matcher` against `actual` and returns the explanation it wrote to its
// result listener, so a test can assert on a failure message.
std::string ExplainMismatch(const Matcher<std::string>& matcher, const std::string& actual) {
  StringMatchResultListener listener;
  matcher.MatchAndExplain(actual, &listener);
  return listener.str();
}

TEST_F(JsonMatcherTest, ObjectKeyOrderIsIgnored) {
  EXPECT_THAT(  // NL
      R"json({"file": "a.cc", "directory": "/work"})json",
      EqJson(R"json({"directory": "/work", "file": "a.cc"})json"));
}

TEST_F(JsonMatcherTest, InsignificantWhitespaceIsIgnored) {
  EXPECT_THAT(  // NL
      "[\n  {\n    \"file\": \"a.cc\"\n  }\n]\n",
      EqJson(R"json([{"file": "a.cc"}])json"));
}

TEST_F(JsonMatcherTest, NestedObjectsAndArraysMatch) {
  EXPECT_THAT(  // NL
      R"json({"a": [1, 2, {"y": null, "x": true}], "b": "s"})json",
      EqJson(R"json({"b": "s", "a": [1, 2, {"x": true, "y": null}]})json"));
}

TEST_F(JsonMatcherTest, DifferentScalarValueDoesNotMatch) {
  EXPECT_THAT(  // NL
      R"json({"file": "a.cc"})json",
      Not(EqJson(R"json({"file": "b.cc"})json")));
}

TEST_F(JsonMatcherTest, ArrayOrderIsSignificant) {
  EXPECT_THAT(  // NL
      R"json(["a", "b"])json",
      Not(EqJson(R"json(["b", "a"])json")));
}

TEST_F(JsonMatcherTest, InvalidActualJsonFailsWithClearMessage) {
  const Matcher<std::string> matcher = EqJson(R"json(["a.cc"])json");
  EXPECT_THAT("[not valid json", Not(matcher));
  EXPECT_THAT(ExplainMismatch(matcher, "[not valid json"), HasSubstr("actual output is not valid JSON"));
}

TEST_F(JsonMatcherTest, InvalidExpectedJsonFailsWithClearMessage) {
  const Matcher<std::string> matcher = EqJson("[not valid json");
  EXPECT_THAT(R"json(["a.cc"])json", Not(matcher));
  EXPECT_THAT(  // NL
      ExplainMismatch(matcher, R"json(["a.cc"])json"),
      HasSubstr("expected JSON is not valid JSON"));
}

TEST_F(JsonMatcherTest, MismatchMessageIncludesBothJsonStrings) {
  const Matcher<std::string> matcher = EqJson(R"json(["b.cc"])json");
  const std::string explanation = ExplainMismatch(matcher, R"json(["a.cc"])json");
  EXPECT_THAT(explanation, HasSubstr("a.cc"));
  EXPECT_THAT(explanation, HasSubstr("b.cc"));
}

}  // namespace
}  // namespace carve::cdb
