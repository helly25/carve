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

#ifndef CARVE_CDB_JSON_MATCHER_H_
#define CARVE_CDB_JSON_MATCHER_H_

#include <ostream>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/util/json_util.h"
#include "google/protobuf/util/message_differencer.h"

namespace carve::cdb {
namespace json_matcher_internal {

// Parses `json` into `value` as the arbitrary-JSON well-known type
// `google::protobuf::Value` (an object maps to a Struct, an array to a
// ListValue), returning the parse status. Default parse options suffice: a
// generic `Value` accepts any well-formed JSON, so there are no unknown fields
// to reject.
inline absl::Status ParseJsonValue(std::string_view json, google::protobuf::Value& value) {
  return google::protobuf::util::JsonStringToMessage(json, &value);
}

// Implementation of the `EqJson` matcher (see below). Owns a copy of the
// expected JSON so the matcher outlives the `EqJson(...)` call argument. The
// subject is a `std::string` because that is what `carve::cdb::ToJson` returns;
// `MatcherInterface<std::string>` fixes the `MatchAndExplain` subject as a
// by-value `std::string` (the interface signature is `MatchAndExplain(T x)`).
class EqJsonMatcher : public ::testing::MatcherInterface<std::string> {
 public:
  explicit EqJsonMatcher(std::string_view expected_json) : expected_json_(expected_json) {}

  void DescribeTo(std::ostream* os) const override { *os << "matches the expected JSON semantically"; }

  void DescribeNegationTo(std::ostream* os) const override { *os << "does not match the expected JSON semantically"; }

  bool MatchAndExplain(std::string actual_json, ::testing::MatchResultListener* listener) const override {
    google::protobuf::Value actual_value;
    const absl::Status actual_status = ParseJsonValue(actual_json, actual_value);
    if (!actual_status.ok()) {
      *listener << "actual output is not valid JSON: " << actual_status.message() << "\nactual JSON was:\n"
                << actual_json;
      return false;
    }

    google::protobuf::Value expected_value;
    const absl::Status expected_status = ParseJsonValue(expected_json_, expected_value);
    if (!expected_status.ok()) {
      *listener << "expected JSON is not valid JSON: " << expected_status.message() << "\nexpected JSON was:\n"
                << expected_json_;
      return false;
    }

    std::string diff;
    google::protobuf::util::MessageDifferencer differencer;
    differencer.ReportDifferencesToString(&diff);
    if (!differencer.Compare(expected_value, actual_value)) {
      *listener << "JSON differs (expected vs actual):\n"
                << diff << "\nactual JSON was:\n"
                << actual_json << "\nexpected JSON was:\n"
                << expected_json_;
      return false;
    }
    return true;
  }

 private:
  const std::string expected_json_;
};

}  // namespace json_matcher_internal

// Matches a JSON string (e.g. the output of `carve::cdb::ToJson`) against
// `expected_json` SEMANTICALLY: both sides are parsed into a
// `google::protobuf::Value` and compared with
// `google::protobuf::util::MessageDifferencer`. Object keys compare
// order-independently (they map to a Struct's map), array elements compare in
// order (they map to a ListValue), and insignificant whitespace is ignored.
// This frees the `expected_json` literal from byte-matching the emitter: write
// it naturally and let clang-format own its layout (see the `R"json(` entry in
// `.clang-format`).
//
// On a parse failure the matcher reports which side was invalid, the offending
// input, and the protobuf parse status. On a semantic mismatch it reports the
// `MessageDifferencer` diff.
//
// KNOWN GAP (schema): a generic `Value` gives semantic equality but does NOT
// validate the compile_commands.json SCHEMA -- unknown or mistyped keys are
// accepted as plain data rather than rejected. A future follow-up could parse
// into a typed `CompileCommands` proto with `ignore_unknown_fields = false` to
// assert schema conformance. That typed proto is intentionally not built here.
inline ::testing::Matcher<std::string> EqJson(std::string_view expected_json) {
  return ::testing::MakeMatcher(new json_matcher_internal::EqJsonMatcher(expected_json));
}

}  // namespace carve::cdb

#endif  // CARVE_CDB_JSON_MATCHER_H_
