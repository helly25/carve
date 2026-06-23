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

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "absl/types/span.h"

namespace carve::command {
namespace {

// Flags dropped on an exact match (no value).
constexpr std::array<std::string_view, 1> kDropExact = {
    "-fno-canonical-system-headers",
};

// Flags whose `<flag>=<value>` joined form is dropped (prefix match).
constexpr std::array<std::string_view, 1> kDropJoinedPrefix = {
    "--gcc-toolchain=",
};

// Flags that consume the following argv token as their value; both the flag and
// its value are dropped.
constexpr std::array<std::string_view, 2> kDropWithValue = {
    "-gcc-toolchain",
    "--gcc-toolchain",
};

template<std::size_t N>
bool Contains(const std::array<std::string_view, N>& set, std::string_view value) {
  for (std::string_view candidate : set) {
    if (candidate == value) {
      return true;
    }
  }
  return false;
}

template<std::size_t N>
bool StartsWithAny(const std::array<std::string_view, N>& prefixes, std::string_view value) {
  for (std::string_view prefix : prefixes) {
    if (value.starts_with(prefix)) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::vector<std::string> DeBazel(absl::Span<const std::string> argv) {
  std::vector<std::string> result;
  result.reserve(argv.size());
  for (std::size_t i = 0; i < argv.size(); ++i) {
    const std::string& arg = argv[i];
    if (Contains(kDropExact, arg) || StartsWithAny(kDropJoinedPrefix, arg)) {
      continue;
    }
    if (Contains(kDropWithValue, arg)) {
      // Also drop the following value token when present.
      if (i + 1 < argv.size()) {
        ++i;
      }
      continue;
    }
    result.push_back(arg);
  }
  return result;
}

}  // namespace carve::command
