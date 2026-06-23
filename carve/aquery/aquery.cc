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

#include "carve/aquery/aquery.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "carve/third_party/bazel/analysis_v2.pb.h"

namespace carve::aquery {
namespace {

bool IsCompileMnemonic(std::string_view mnemonic) {
  static const absl::flat_hash_set<std::string_view>* const kCompile =
      new absl::flat_hash_set<std::string_view>{"CppCompile", "ObjcCompile", "CppModuleCompile"};
  return kCompile->contains(mnemonic);
}

// Expands response-file references (`@exec_path`) using the param files aquery
// embedded in the action (requires `--include_param_files`). Tokens whose
// referenced param file is absent are left verbatim, so this is a no-op when
// param files were not requested. Nested references are expanded iteratively
// with a guard against cycles.
std::vector<std::string> ExpandParamFiles(const analysis::Action& action) {
  if (action.param_files().empty()) {
    return std::vector<std::string>(action.arguments().begin(), action.arguments().end());
  }
  absl::flat_hash_map<std::string_view, const analysis::ParamFile*> by_exec_path;
  by_exec_path.reserve(static_cast<std::size_t>(action.param_files_size()));
  for (const analysis::ParamFile& param_file : action.param_files()) {
    by_exec_path.emplace(param_file.exec_path(), &param_file);
  }

  std::vector<std::string> result;
  std::size_t expansions = 0;
  const std::size_t expansion_cap = by_exec_path.size() + action.arguments_size() + 1;
  // Worklist of pending argument ranges; start with the action's own arguments.
  std::vector<std::vector<std::string_view>> stack;
  std::vector<std::string_view> initial(action.arguments().begin(), action.arguments().end());
  std::reverse(initial.begin(), initial.end());
  stack.push_back(std::move(initial));
  while (!stack.empty()) {
    if (stack.back().empty()) {
      stack.pop_back();
      continue;
    }
    const std::string_view arg = stack.back().back();
    stack.back().pop_back();
    if (arg.size() > 1 && arg.front() == '@' && expansions < expansion_cap) {
      const auto it = by_exec_path.find(arg.substr(1));
      if (it != by_exec_path.end()) {
        ++expansions;
        std::vector<std::string_view> nested(it->second->arguments().begin(), it->second->arguments().end());
        std::reverse(nested.begin(), nested.end());
        stack.push_back(std::move(nested));
        continue;
      }
    }
    result.emplace_back(arg);
  }
  return result;
}

// Resolves an exec-root-relative path from a path-fragment id by walking the
// parent chain (id 0 marks the root sentinel). Returns empty if the id is
// unknown. The iteration cap defends against a malformed (cyclic) graph.
std::string ResolvePath(
    std::uint32_t fragment_id,
    const absl::flat_hash_map<std::uint32_t, const analysis::PathFragment*>& by_id) {
  std::vector<std::string_view> parts;
  std::uint32_t id = fragment_id;
  for (std::size_t guard = 0; guard <= by_id.size() && id != 0; ++guard) {
    const auto it = by_id.find(id);
    if (it == by_id.end()) {
      break;
    }
    parts.emplace_back(it->second->label());
    id = it->second->parent_id();
  }
  std::string path;
  for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
    if (!path.empty()) {
      path.push_back('/');
    }
    path.append(it->data(), it->size());
  }
  return path;
}

}  // namespace

absl::StatusOr<std::vector<CompileAction>> ParseCompileActions(std::string_view serialized) {
  analysis::ActionGraphContainer container;
  if (!container.ParseFromArray(serialized.data(), static_cast<int>(serialized.size()))) {
    return absl::InvalidArgumentError("failed to parse aquery ActionGraphContainer proto");
  }

  absl::flat_hash_map<std::uint32_t, const analysis::PathFragment*> fragment_by_id;
  fragment_by_id.reserve(static_cast<std::size_t>(container.path_fragments_size()));
  for (const analysis::PathFragment& fragment : container.path_fragments()) {
    fragment_by_id.emplace(fragment.id(), &fragment);
  }

  absl::flat_hash_map<std::uint32_t, std::uint32_t> artifact_fragment_by_id;
  artifact_fragment_by_id.reserve(static_cast<std::size_t>(container.artifacts_size()));
  for (const analysis::Artifact& artifact : container.artifacts()) {
    artifact_fragment_by_id.emplace(artifact.id(), artifact.path_fragment_id());
  }

  std::vector<CompileAction> result;
  for (const analysis::Action& action : container.actions()) {
    if (!IsCompileMnemonic(action.mnemonic())) {
      continue;
    }
    CompileAction out;
    out.action_key = action.action_key();
    out.mnemonic = action.mnemonic();
    out.arguments = ExpandParamFiles(action);
    const auto art_it = artifact_fragment_by_id.find(action.primary_output_id());
    if (art_it != artifact_fragment_by_id.end()) {
      out.primary_output = ResolvePath(art_it->second, fragment_by_id);
    }
    result.push_back(std::move(out));
  }
  return result;
}

}  // namespace carve::aquery
