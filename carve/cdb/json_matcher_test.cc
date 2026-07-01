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

// An integer and the same value written as a float parse to the same
// `google::protobuf::Value`: JSON has a single number type, backed by a double,
// so `1` and `1.0` compare equal.
TEST_F(JsonMatcherTest, IntegerAndFloatNumbersCompareEqual) {
  EXPECT_THAT(R"json([1])json", EqJson(R"json([1.0])json"));
}

// A number and the string spelling of that number are different JSON types.
TEST_F(JsonMatcherTest, NumberDoesNotMatchStringWithSameDigits) {
  EXPECT_THAT(R"json([1])json", Not(EqJson(R"json(["1"])json")));
}

TEST_F(JsonMatcherTest, BooleansMatchThemselves) {
  EXPECT_THAT(  // NL
      R"json({"t": true, "f": false})json",
      EqJson(R"json({"f": false, "t": true})json"));
}

// A boolean and the string spelling of that boolean are different JSON types.
TEST_F(JsonMatcherTest, BooleanDoesNotMatchStringTrue) {
  EXPECT_THAT(R"json([true])json", Not(EqJson(R"json(["true"])json")));
}

TEST_F(JsonMatcherTest, NullMatchesNull) {
  EXPECT_THAT(R"json([null])json", EqJson(R"json([null])json"));
}

// An explicit `null` value for a key differs from that key being absent: the
// two objects below are equal except that one carries `"x": null` and the other
// omits `x` entirely, and they do not match.
TEST_F(JsonMatcherTest, NullValueDiffersFromAbsentKey) {
  EXPECT_THAT(  // NL
      R"json({"x": null, "y": 1})json",
      Not(EqJson(R"json({"y": 1})json")));
}

// `null` is its own JSON type, distinct from the number 0 and the empty string.
TEST_F(JsonMatcherTest, NullDoesNotMatchZeroOrEmptyString) {
  EXPECT_THAT(R"json([null])json", Not(EqJson(R"json([0])json")));
  EXPECT_THAT(R"json([null])json", Not(EqJson(R"json([""])json")));
}

// An empty array and an empty object are different JSON types (ListValue vs
// Struct) and do not match. They are wrapped in an outer array purely so
// clang-format keeps each document on a single line.
TEST_F(JsonMatcherTest, EmptyArrayDoesNotMatchEmptyObject) {
  EXPECT_THAT(R"json([[]])json", Not(EqJson(R"json([{}])json")));
}

TEST_F(JsonMatcherTest, EmptyContainerDoesNotMatchNonEmpty) {
  EXPECT_THAT(R"json([])json", Not(EqJson(R"json([1])json")));
  EXPECT_THAT(  // NL
      R"json({"a": [], "z": 0})json",
      Not(EqJson(R"json({"a": [1], "z": 0})json")));
}

// Escaped and literal spellings of the same characters (a newline, a quote, and
// a non-ASCII code point) parse to the same string, so they compare equal.
TEST_F(JsonMatcherTest, EscapedAndLiteralCharactersCompareEqual) {
  EXPECT_THAT("[\"line1\nline2\"]", EqJson(R"json(["line1\nline2"])json"));
  EXPECT_THAT(R"json(["a\"b"])json", EqJson("[\"a\\\"b\"]"));
  EXPECT_THAT("[\"caf\xc3\xa9\"]", EqJson(R"json(["café"])json"));
}

// protobuf's JSON parser REJECTS duplicate object keys rather than applying a
// last-wins (or first-wins) rule, so a duplicate-key input is reported as
// invalid JSON instead of silently collapsing to one value. The matcher's
// expected JSON is therefore never reached and its content is immaterial.
TEST_F(JsonMatcherTest, DuplicateKeysAreRejectedAsInvalidJson) {
  const Matcher<std::string> matcher = EqJson(R"json(["unused"])json");
  EXPECT_THAT(  // NL
      R"json({"x": 1, "x": 2})json",
      Not(matcher));
  EXPECT_THAT(  // NL
      ExplainMismatch(matcher, R"json({"x": 1, "x": 2})json"),
      HasSubstr("actual output is not valid JSON"));
}

// Array element order is significant while object key order is not, shown in one
// nested document: reordering the object keys still matches, but reversing the
// array does not.
TEST_F(JsonMatcherTest, ArrayOrderMattersButKeyOrderDoesNotWhenNested) {
  EXPECT_THAT(  // NL
      R"json({"a": [1, 2], "b": {"p": 1, "q": 2}})json",
      EqJson(R"json({"b": {"q": 2, "p": 1}, "a": [1, 2]})json"));
  EXPECT_THAT(  // NL
      R"json({"a": [1, 2], "b": {"p": 1, "q": 2}})json",
      Not(EqJson(R"json({"b": {"q": 2, "p": 1}, "a": [2, 1]})json")));
}

// Indentation and inter-token whitespace are not significant: a pretty-printed
// document (the escaped literal) matches the same document written compactly.
TEST_F(JsonMatcherTest, IndentationAndWhitespaceAreNotSignificant) {
  EXPECT_THAT(  // NL
      "{\n    \"a\" : [ 1 , 2 ] ,\n    \"b\" : \"s\"\n}\n",
      EqJson(R"json({"a": [1, 2], "b": "s"})json"));
}

// A deeply nested structure matches itself under key reordering at every level.
TEST_F(JsonMatcherTest, DeeplyNestedStructuresMatch) {
  EXPECT_THAT(  // NL
      R"json({"a": {"b": {"c": [{"d": 1, "e": [2, 3]}]}}})json",
      EqJson(R"json({"a": {"b": {"c": [{"e": [2, 3], "d": 1}]}}})json"));
}

}  // namespace
}  // namespace carve::cdb
