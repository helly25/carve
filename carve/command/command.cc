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
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/str_replace.h"
#include "absl/types/span.h"

namespace carve::command {
namespace {

// Flags dropped on an exact match (no value).
constexpr std::array<std::string_view, 3> kDropExact = {
    "-fno-canonical-system-headers",
    "/showIncludes",       // MSVC dependency listing (Ninja issue 613).
    "/showIncludes:user",  // ... and its user-headers-only variant.
};

// Flags whose `<flag>=<value>` joined form is dropped (prefix match).
constexpr std::array<std::string_view, 2> kDropJoinedPrefix = {
    "--gcc-toolchain=",
    "-fmodules-cache-path=bazel-out/",  // Per-build clang modules cache dir.
};

// Flags that consume the following argv token as their value; both are dropped.
constexpr std::array<std::string_view, 2> kDropWithValue = {
    "-gcc-toolchain",
    "--gcc-toolchain",
};

// True if `compiler` is a (possibly path-qualified) `ccache` wrapper. Lexical
// only: `path::filename` parses the string, it does not touch the filesystem.
bool IsCcache(std::string_view compiler) {
  return std::filesystem::path(compiler).filename() == "ccache";
}

}  // namespace

std::vector<std::string> DeBazel(absl::Span<const std::string> argv) {
  std::vector<std::string> result;
  result.reserve(argv.size());
  for (std::size_t i = 0; i < argv.size(); ++i) {
    const std::string& arg = argv[i];
    // A leading `ccache` is a compiler wrapper (and only ever the first token);
    // drop it so argv[0] is the real compiler clangd can introspect.
    if (i == 0 && IsCcache(arg)) {
      continue;
    }
    if (absl::c_linear_search(kDropExact, arg)
        || absl::c_any_of(kDropJoinedPrefix, [&arg](std::string_view prefix) { return arg.starts_with(prefix); })) {
      continue;
    }
    if (absl::c_linear_search(kDropWithValue, arg)) {
      if (i + 1 < argv.size()) {
        ++i;  // Also drop the value token.
      }
      continue;
    }
    result.push_back(arg);
  }
  return result;
}

std::vector<std::string> ResolveXcodePlaceholders(
    absl::Span<const std::string> argv,
    std::string_view developer_dir,
    std::string_view sdkroot) {
  std::vector<std::pair<std::string, std::string>> subs;
  if (!developer_dir.empty()) {
    subs.emplace_back("__BAZEL_XCODE_DEVELOPER_DIR__", std::string(developer_dir));
  }
  if (!sdkroot.empty()) {
    subs.emplace_back("__BAZEL_XCODE_SDKROOT__", std::string(sdkroot));
  }
  std::vector<std::string> result;
  result.reserve(argv.size());
  for (const std::string& arg : argv) {
    result.push_back(subs.empty() ? arg : absl::StrReplaceAll(arg, subs));
  }
  return result;
}

}  // namespace carve::command
