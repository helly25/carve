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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace carve::io {
namespace {

// Returns a temporary path in the same directory as `path`, unique enough to
// avoid collisions between concurrent writers within a process.
std::filesystem::path TempSibling(const std::filesystem::path& path) {
  static std::atomic<std::uint64_t> counter{0};
  const auto stamp = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::uint64_t seq = counter.fetch_add(1, std::memory_order_relaxed);
  std::filesystem::path tmp = path;
  tmp += absl::StrCat(".carve-tmp-", stamp, "-", seq);
  return tmp;
}

}  // namespace

absl::Status WriteAtomically(const std::filesystem::path& path, std::string_view content) {
  std::error_code error_code;
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error_code);
    if (error_code) {
      return absl::UnknownError(
          absl::StrCat("failed to create directory '", parent.string(), "': ", error_code.message()));
    }
  }

  const std::filesystem::path tmp = TempSibling(path);
  {
    std::ofstream stream(tmp, std::ios::binary | std::ios::trunc);
    if (!stream) {
      return absl::UnknownError(absl::StrCat("failed to open temp file '", tmp.string(), "'"));
    }
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    stream.close();
    if (!stream) {
      std::filesystem::remove(tmp, error_code);
      return absl::UnknownError(absl::StrCat("failed to write temp file '", tmp.string(), "'"));
    }
  }

  std::filesystem::rename(tmp, path, error_code);
  if (error_code) {
    std::filesystem::remove(tmp, error_code);
    return absl::UnknownError(absl::StrCat("failed to rename '", tmp.string(), "' to '", path.string(), "'"));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> ReadFile(const std::filesystem::path& path) {
  std::error_code error_code;
  if (!std::filesystem::exists(path, error_code) || error_code) {
    return absl::NotFoundError(absl::StrCat("no such file '", path.string(), "'"));
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return absl::UnknownError(absl::StrCat("cannot open '", path.string(), "'"));
  }
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

}  // namespace carve::io
