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

#include "carve/refresh/refresh.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "carve/aquery/aquery.h"
#include "carve/cdb/cdb.h"
#include "carve/command/command.h"

namespace carve::refresh {
namespace {

// True if `arg` names a C/C++/Objective-C/CUDA translation unit by extension.
bool IsSourceFile(std::string_view arg) {
  const std::string_view::size_type dot = arg.rfind('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  const std::string_view ext = arg.substr(dot + 1);
  static const absl::flat_hash_set<std::string_view>* const kSourceExt =
      new absl::flat_hash_set<std::string_view>{"c", "cc", "cpp", "cxx", "c++", "cu", "m", "mm"};
  return kSourceExt->contains(ext);
}

// Returns the source operand of a compile command: the last argv token that
// looks like a source file. Empty if none is present.
std::string_view FindSource(const std::vector<std::string>& argv) {
  std::string_view source;
  for (const std::string& arg : argv) {
    if (IsSourceFile(arg)) {
      source = arg;
    }
  }
  return source;
}

}  // namespace

absl::StatusOr<std::vector<cdb::CompileCommand>> BuildEntries(std::string_view aquery_proto,
                                                              const Options& options) {
  absl::StatusOr<std::vector<aquery::CompileAction>> actions =
      aquery::ParseCompileActions(aquery_proto);
  if (!actions.ok()) {
    return actions.status();
  }

  std::vector<cdb::CompileCommand> entries;
  entries.reserve(actions->size());
  for (const aquery::CompileAction& action : *actions) {
    std::vector<std::string> arguments = command::DeBazel(action.arguments);
    const std::string_view source = FindSource(arguments);
    if (source.empty()) {
      continue;
    }
    entries.push_back(cdb::CompileCommand{
        .directory = options.directory,
        .file = std::string(source),
        .arguments = std::move(arguments),
        .output = action.primary_output,
    });
  }
  return entries;
}

}  // namespace carve::refresh
