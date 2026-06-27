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

#include "carve/scan_deps/scan_deps.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "carve/command/command.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/DependencyScanning/DependencyScanningService.h"
#include "clang/Tooling/DependencyScanningTool.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"

namespace carve::scan_deps {
namespace {

// LLVM 22 split the API: the service + enums live in clang::dependencies, the
// tool in clang::tooling. Scan errors are reported to a DiagnosticConsumer
// rather than returned, so we capture them into a string for the status.
namespace deps = clang::dependencies;

class CapturingDiagnosticConsumer : public clang::DiagnosticConsumer {
 public:
  explicit CapturingDiagnosticConsumer(std::string& sink) : sink_(sink) {}

  void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic& info) override {
    clang::DiagnosticConsumer::HandleDiagnostic(level, info);
    if (level < clang::DiagnosticsEngine::Error) {
      return;
    }
    constexpr unsigned kMessageInlineCapacity = 256;
    llvm::SmallString<kMessageInlineCapacity> message;
    info.FormatDiagnostic(message);
    if (!sink_.empty()) {
      sink_.push_back('\n');
    }
    sink_.append(message.data(), message.size());
  }

 private:
  std::string& sink_;
};

}  // namespace

absl::StatusOr<std::vector<std::string>> ScanDependencies(
    absl::Span<const std::string> args,
    std::string_view working_dir) {
  deps::DependencyScanningService service(
      deps::ScanningMode::DependencyDirectivesScan, deps::ScanningOutputFormat::Make);
  clang::tooling::DependencyScanningTool tool(service);

  std::string diagnostics;
  CapturingDiagnosticConsumer diag_consumer(diagnostics);
  const std::vector<std::string> command(args.begin(), args.end());
  const std::optional<std::string> result =
      tool.getDependencyFile(command, llvm::StringRef(working_dir.data(), working_dir.size()), diag_consumer);
  if (!result.has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("scan-deps failed: ", diagnostics));
  }
  return command::ParseMakeDependencies(*result);
}

}  // namespace carve::scan_deps
