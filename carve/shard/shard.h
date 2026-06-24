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

#ifndef CARVE_SHARD_SHARD_H_
#define CARVE_SHARD_SHARD_H_

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "carve/sidecar/carve.pb.h"

namespace carve::shard {

// Scans one compile command's dependencies: (argv, working_dir) -> header paths.
// Injected so tests need no real toolchain; the binary passes
// `scan_deps::ScanDependencies`. Same contract as `refresh`'s scanner.
using HeaderScanner =
    std::function<absl::StatusOr<std::vector<std::string>>(absl::Span<const std::string>, std::string_view)>;

// Returns the current unix time in seconds; injected for deterministic tests.
using Clock = std::function<std::int64_t()>;

// Inputs for building one shard (the record for a single compile action).
struct Options {
  std::string action_key;            // the action's identity key
  std::vector<std::string> command;  // raw (Bazel-emitted) compiler argv
  std::string source;                // exec-root-relative source path
  std::string project_id;            // scopes the record; empty = the default project
  std::string primary_output;        // exec-root-relative primary output; optional
  std::string directory;             // execroot the scan resolves relative paths against
  std::string xcode_developer_dir;   // resolves `__BAZEL_XCODE_DEVELOPER_DIR__`; optional
  std::string xcode_sdkroot;         // resolves `__BAZEL_XCODE_SDKROOT__`; optional
  HeaderScanner scanner;             // optional; null skips header scanning
  Clock clock;                       // optional; null leaves `written_at` unset
};

// Builds a single-record shard from one compile action: de-Bazel the command,
// resolve Apple `wrapped_clang` placeholders, scan headers (a failed scan records
// none and leaves the row unstamped, so a later run re-scans it — cache only a
// complete scan), and stamp `written_at` when the scan is complete (or when no
// scanner is configured). The record shape matches what `refresh` produces, so
// shards and refreshed sidecars merge cleanly in `aggregate`.
[[nodiscard]] ActionRecords BuildShard(const Options& options);

// File-facing form for the CLI: reads the raw argv from `command_file` (one token
// per line, the Bazel "multiline" param-file format) and atomically writes the
// shard returned by `BuildShard` to `out_path`.
struct FileOptions {
  std::string action_key;
  std::string command_file;
  std::string source;
  std::string project_id;
  std::string primary_output;
  std::string directory;
  std::string xcode_developer_dir;
  std::string xcode_sdkroot;
  HeaderScanner scanner;
  Clock clock;
  std::string out_path;
};

[[nodiscard]] absl::Status RunShard(const FileOptions& options);

}  // namespace carve::shard

#endif  // CARVE_SHARD_SHARD_H_
