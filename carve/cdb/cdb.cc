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

#include "carve/cdb/cdb.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "carve/io/io.h"

namespace carve::cdb {
namespace {

// Appends `value` to `out` as a JSON string literal, including the surrounding
// quotes, escaping per RFC 8259. UTF-8 continuation bytes pass through verbatim.
void AppendJsonString(std::string& out, std::string_view value) {
  // Code points below this are control characters that must be escaped; at or
  // above it the byte is emitted verbatim (bar the cases handled by the switch).
  constexpr unsigned kFirstUnescapedAscii = 0x20;
  constexpr unsigned kLowNibbleMask = 0x0FU;
  constexpr unsigned kNibbleBits = 4U;
  static constexpr std::array<char, 16> kHexDigits = {'0', '1', '2', '3', '4', '5', '6', '7',
                                                      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  out.push_back('"');
  for (const char character : value) {
    const unsigned byte = static_cast<unsigned char>(character);
    switch (character) {
      case '"': out.append("\\\""); break;
      case '\\': out.append("\\\\"); break;
      case '\b': out.append("\\b"); break;
      case '\f': out.append("\\f"); break;
      case '\n': out.append("\\n"); break;
      case '\r': out.append("\\r"); break;
      case '\t': out.append("\\t"); break;
      default:
        if (byte < kFirstUnescapedAscii) {
          // Other control characters require the \u00XX escape form.
          out.append("\\u00");
          out.push_back(kHexDigits[byte >> kNibbleBits]);
          out.push_back(kHexDigits[byte & kLowNibbleMask]);
        } else {
          out.push_back(character);
        }
        break;
    }
  }
  out.push_back('"');
}

// indent and key are distinct roles, fixed at every call site.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void AppendKey(std::string& out, std::string_view indent, std::string_view key) {
  out.append(indent);
  AppendJsonString(out, key);
  out.append(": ");
}

}  // namespace

std::string ToJson(absl::Span<const CompileCommand> entries) {
  if (entries.empty()) {
    return "[]\n";
  }
  std::string out = "[\n";
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const CompileCommand& entry = entries[i];
    out.append("  {\n");

    AppendKey(out, "    ", "directory");
    AppendJsonString(out, entry.directory);
    out.append(",\n");

    AppendKey(out, "    ", "file");
    AppendJsonString(out, entry.file);

    if (!entry.arguments.empty()) {
      out.append(",\n");
      AppendKey(out, "    ", "arguments");
      out.append("[\n");
      for (std::size_t arg_index = 0; arg_index < entry.arguments.size(); ++arg_index) {
        out.append("      ");
        AppendJsonString(out, entry.arguments[arg_index]);
        out.append(arg_index + 1 < entry.arguments.size() ? ",\n" : "\n");
      }
      out.append("    ]");
    }

    if (!entry.output.empty()) {
      out.append(",\n");
      AppendKey(out, "    ", "output");
      AppendJsonString(out, entry.output);
    }

    out.append("\n  }");
    out.append(i + 1 < entries.size() ? ",\n" : "\n");
  }
  out.append("]\n");
  return out;
}

absl::Status Write(const std::filesystem::path& path, absl::Span<const CompileCommand> entries) {
  return io::WriteAtomically(path, ToJson(entries));
}

}  // namespace carve::cdb
