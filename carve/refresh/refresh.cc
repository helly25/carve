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

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/mutex.h"
#include "carve/aquery/aquery.h"
#include "carve/cdb/cdb.h"
#include "carve/command/command.h"
#include "carve/io/io.h"
#include "carve/process/process.h"
#include "carve/sidecar/carve.pb.h"
#include "carve/sidecar/sidecar.h"
#include "mbo/status/status_macros.h"

namespace carve::refresh {
namespace {

// True if `arg` names a C/C++/Objective-C/CUDA translation unit by extension.
bool IsSourceFile(std::string_view arg) {
  const std::string_view::size_type dot = arg.rfind('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  std::string_view ext = arg.substr(dot + 1);
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

// Resolves a (possibly exec-root-relative) source path to an absolute path so
// clangd matches it unambiguously. A path that is already absolute, or any path
// when `directory` is empty, is returned unchanged.
std::string AbsoluteFile(std::string_view directory, std::string_view source) {
  const std::filesystem::path path(source);
  if (directory.empty() || path.is_absolute()) {
    return std::string(source);
  }
  return (std::filesystem::path(directory) / path).lexically_normal().string();
}

// Maps one compile action to an ActionRecord, de-Bazeling its argv and detecting
// the source operand. Headers are NOT scanned here; `RunRefresh` scans only the
// added/changed records (see `ScanHeaders`). Returns nullopt when no source can
// be identified (such an action cannot form a valid compilation-database entry).
std::optional<ActionRecord> MakeRecord(const aquery::CompileAction& action, std::string_view project_id) {
  std::vector<std::string> arguments = command::DeBazel(action.arguments);
  std::string_view source = FindSource(arguments);
  if (source.empty()) {
    return std::nullopt;
  }
  ActionRecord record;
  record.set_action_key(action.action_key);
  record.add_sources(std::string(source));  // Copies before `arguments` is moved.
  for (std::string& arg : arguments) {
    record.add_command(std::move(arg));
  }
  // Edition 2024 fields have explicit presence, so only set optional fields when
  // they exist; otherwise an empty value would serialize and differ from a
  // record that never had one.
  if (!action.primary_output.empty()) {
    record.set_primary_output(action.primary_output);
  }
  if (!project_id.empty()) {
    record.set_project_id(project_id);
  }
  return record;
}

// Builds the current action records (all stamped with `project_id`, no headers
// yet) from serialized aquery bytes.
absl::StatusOr<ActionRecords> BuildRecords(std::string_view aquery_proto, std::string_view project_id) {
  MBO_ASSIGN_OR_RETURN(const std::vector<aquery::CompileAction> actions, aquery::ParseCompileActions(aquery_proto));
  ActionRecords records;
  for (const aquery::CompileAction& action : actions) {
    std::optional<ActionRecord> record = MakeRecord(action, project_id);
    if (record.has_value()) {
      *records.add_records() = *std::move(record);
    }
  }
  return records;
}

// Substitutes Apple `wrapped_clang` placeholders in every record's command so
// the commands clangd and scan-deps see carry real SDK/developer paths. The
// resolver (IO: `xcode-select`/`xcrun`) is only invoked when some command
// actually contains a `__BAZEL_XCODE_` placeholder, so non-Apple builds pay
// nothing. See CARVE_DESIGN.md section 4.3.
void ApplyXcodePlaceholders(ActionRecords& records, const XcodeResolver& resolver) {
  const auto has_placeholder = [](const ActionRecord& record) {
    return absl::c_any_of(
        record.command(), [](std::string_view arg) { return absl::StrContains(arg, "__BAZEL_XCODE_"); });
  };
  if (!absl::c_any_of(records.records(), has_placeholder)) {
    return;
  }
  const XcodePaths xcode = resolver();
  for (ActionRecord& record : *records.mutable_records()) {
    const std::vector<std::string> command(record.command().begin(), record.command().end());
    record.clear_command();
    for (const std::string& arg : command::ResolveXcodePlaceholders(command, xcode.developer_dir, xcode.sdkroot)) {
      record.add_command(arg);
    }
  }
}

// Scans `record`'s command for header dependencies (against `directory`) and
// stores them on the record. Returns true if the scan succeeded; false if it
// failed (e.g. an unbuilt generated header that scan-deps cannot resolve), in
// which case `headers` is left unset and the caller should not treat the record
// as completely cached (CARVE_DESIGN.md section 4.2).
bool ScanHeaders(ActionRecord& record, std::string_view directory, const HeaderScanner& scanner) {
  const std::vector<std::string> argv(record.command().begin(), record.command().end());
  absl::StatusOr<std::vector<std::string>> headers = scanner(argv, directory);
  if (!headers.ok()) {
    return false;
  }
  for (const std::string& header : *headers) {
    record.add_headers(header);
  }
  return true;
}

// Scans a fixed set of action records concurrently. Each worker repeatedly
// claims the next unscanned record and scans it; `workers` threads run at once
// (the scanner must be safe to call concurrently — see HeaderScanner).
//
// The single mutex `mu_` controls exactly two members: the claim cursor `next_`
// and the failure list `failed_`. Everything else is immutable after
// construction. A record's own contents are mutated WITHOUT the lock: that is
// safe because `ClaimNext` hands each index to exactly one worker, so no two
// workers ever touch the same record (and distinct repeated-field elements have
// disjoint storage — the parent field is never resized here).
class ParallelScan {
 public:
  ParallelScan(std::vector<ActionRecord*> records, std::string_view directory, const HeaderScanner& scanner)
      : records_(std::move(records)), directory_(directory), scanner_(scanner) {}

  // Scans every record using min(`workers`, size) threads (>=1), and returns the
  // ascending indices of records whose scan failed.
  std::vector<std::size_t> Run(unsigned workers) ABSL_LOCKS_EXCLUDED(mu_) {
    if (!records_.empty()) {
      const unsigned threads = std::min<unsigned>(std::max(workers, 1U), records_.size());
      if (threads == 1) {
        Worker();  // Serial: run on the calling thread, spawn nothing.
      } else {
        std::vector<std::thread> pool;
        pool.reserve(threads);
        for (unsigned i = 0; i < threads; ++i) {
          pool.emplace_back([this] { Worker(); });
        }
        for (std::thread& thread : pool) {
          thread.join();
        }
      }
    }
    absl::MutexLock lock(mu_);
    std::sort(failed_.begin(), failed_.end());
    return failed_;
  }

 private:
  // Returns the next record index to scan, or nullopt once all are claimed.
  std::optional<std::size_t> ClaimNext() ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(mu_);
    if (next_ >= records_.size()) {
      return std::nullopt;
    }
    return next_++;
  }

  // Worker loop: claim and scan records until none remain.
  void Worker() ABSL_LOCKS_EXCLUDED(mu_) {
    while (const std::optional<std::size_t> index = ClaimNext()) {
      // Sole owner of records_[*index] (see class comment): scan without the lock.
      if (!ScanHeaders(*records_[*index], directory_, scanner_)) {
        absl::MutexLock lock(mu_);
        failed_.push_back(*index);
      }
    }
  }

  const std::vector<ActionRecord*> records_;
  const std::string_view directory_;
  const HeaderScanner& scanner_;

  absl::Mutex mu_;
  std::size_t next_ ABSL_GUARDED_BY(mu_) = 0;             // Next record index to claim.
  std::vector<std::size_t> failed_ ABSL_GUARDED_BY(mu_);  // Indices of records whose scan failed.
};

// Resolves the configured `jobs` to a worker-thread count (>=1).
unsigned WorkerCount(int jobs) {
  return jobs > 0 ? static_cast<unsigned>(jobs) : 1U;
}

// True if `path` exists and was last modified strictly after `since` (unix
// seconds). A path that cannot be stat'd (missing, unreadable) returns false: we
// cannot prove it changed, so it does not on its own force a re-scan.
bool ModifiedAfter(const std::filesystem::path& path, std::int64_t since) {
  std::error_code error;
  const std::filesystem::file_time_type mtime = std::filesystem::last_write_time(path, error);
  if (error) {
    return false;
  }
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::file_clock::to_sys(mtime).time_since_epoch())
          .count();
  return seconds > since;
}

// True if `stored`'s cached scan can no longer be trusted: any source or header
// it recorded has been modified since `written_at`, so its include set may have
// changed and the action must be re-scanned. An unstamped record (written_at 0)
// has no baseline and is always treated as stale. Sources are resolved against
// `directory` (the execroot); scan-deps headers are already absolute.
//
// `written_at` has one-second granularity, so an edit made in the same second as
// the previous refresh's stamp is not detected until the next edit or refresh —
// acceptable for a CDB cache, and clangd re-preprocesses regardless.
bool CachedScanIsStale(const ActionRecord& stored, std::string_view directory) {
  if (stored.written_at() == 0) {
    return true;  // No baseline timestamp: re-scan to establish one.
  }
  const std::int64_t since = stored.written_at();
  // Sources are execroot-relative; scan-deps headers are already absolute.
  return absl::c_any_of(
             stored.sources(),
             [&](std::string_view source) { return ModifiedAfter(AbsoluteFile(directory, source), since); })
         || absl::c_any_of(stored.headers(), [&](std::string_view header) {
              return ModifiedAfter(std::filesystem::path(header), since);
            });
}

// Returns `bazel info execution_root` (trimmed): the directory clangd should
// resolve each entry's relative paths against.
absl::StatusOr<std::string> BazelExecRoot(const std::string& bazel) {
  MBO_ASSIGN_OR_RETURN(
      process::CommandResult result, process::Run({bazel.empty() ? "bazel" : bazel, "info", "execution_root"}));
  if (result.exit_code != 0) {
    return absl::UnknownError(
        absl::StrCat("bazel info execution_root failed (exit ", result.exit_code, "): ", result.stderr_data));
  }
  std::string root = std::move(result.stdout_data);
  while (!root.empty() && (root.back() == '\n' || root.back() == '\r' || root.back() == ' ')) {
    root.pop_back();
  }
  return root;
}

// Runs `bazel aquery --output=proto` over `targets`, filtered to compile
// mnemonics and with param files embedded, and returns the serialized
// ActionGraphContainer bytes.
absl::StatusOr<std::string> RunAquery(const std::vector<std::string>& targets, const std::string& bazel) {
  const std::string expr =
      absl::StrCat("mnemonic(\"CppCompile|ObjcCompile|CppModuleCompile\", ", absl::StrJoin(targets, " + "), ")");
  const std::vector<std::string> argv = {
      bazel.empty() ? "bazel" : bazel, "aquery", "--output=proto", "--include_param_files", expr};
  MBO_ASSIGN_OR_RETURN(process::CommandResult result, process::Run(argv));
  if (result.exit_code != 0) {
    return absl::UnknownError(absl::StrCat("bazel aquery failed (exit ", result.exit_code, "): ", result.stderr_data));
  }
  return std::move(result.stdout_data);
}

}  // namespace

std::vector<cdb::CompileCommand> EntriesFromRecords(const ActionRecords& records, std::string_view directory) {
  std::vector<cdb::CompileCommand> entries;
  entries.reserve(static_cast<std::size_t>(records.records_size()));
  for (const ActionRecord& record : records.records()) {
    if (record.sources_size() == 0) {
      continue;
    }
    std::vector<std::string> arguments(record.command().begin(), record.command().end());
    entries.push_back(
        cdb::CompileCommand{
            .directory = std::string(directory),
            .file = AbsoluteFile(directory, record.sources(0)),
            .arguments = std::move(arguments),
            .output = std::string(record.primary_output()),
        });
  }
  return entries;
}

absl::StatusOr<std::vector<cdb::CompileCommand>> BuildEntries(std::string_view aquery_proto, const Options& options) {
  MBO_ASSIGN_OR_RETURN(const ActionRecords records, BuildRecords(aquery_proto, options.project_id));
  return EntriesFromRecords(records, options.directory);
}

absl::StatusOr<RefreshStats> RunRefresh(const FileOptions& options) {
  absl::StatusOr<std::string> proto;
  if (!options.aquery_proto_path.empty()) {
    proto = io::ReadFile(options.aquery_proto_path);
  } else if (!options.targets.empty()) {
    proto = RunAquery(options.targets, options.bazel_path);
  } else {
    return absl::InvalidArgumentError("refresh needs --aquery_proto or --targets");
  }
  MBO_RETURN_IF_ERROR(proto.status());  // status() so a large proto is not copied.

  // Default the entry directory to the execroot so exec-relative argv resolve.
  std::string directory = options.directory;
  if (directory.empty()) {
    MBO_ASSIGN_OR_RETURN(std::string execroot, BazelExecRoot(options.bazel_path));
    directory = std::move(execroot);
  }

  MBO_ASSIGN_OR_RETURN(ActionRecords current, BuildRecords(*proto, options.project_id));
  if (options.xcode_resolver) {
    ApplyXcodePlaceholders(current, options.xcode_resolver);
  }

  if (options.sidecar_path.empty()) {
    // No cache: scan every action (when a scanner is configured).
    std::vector<ActionRecord*> to_scan;
    if (options.scanner) {
      for (ActionRecord& record : *current.mutable_records()) {
        to_scan.push_back(&record);
      }
    }
    const std::vector<std::size_t> failed =
        ParallelScan(to_scan, directory, options.scanner).Run(WorkerCount(options.jobs));

    const std::vector<cdb::CompileCommand> entries = EntriesFromRecords(current, directory);
    RefreshStats stats;
    stats.entries = static_cast<int>(entries.size());
    stats.scanned = static_cast<int>(to_scan.size());
    stats.unresolved = static_cast<int>(failed.size());
    MBO_RETURN_IF_ERROR(cdb::Write(options.output_path, entries));
    return stats;
  }

  MBO_ASSIGN_OR_RETURN(const ActionRecords stored, sidecar::Load(options.sidecar_path));

  // Decide serially which actions need a (re)scan: added/changed actions, plus
  // unchanged actions whose cached headers (or source) were edited on disk since
  // the scan was recorded. Truly unchanged actions reuse the stored record's
  // cached headers via MergeRecords below — the incremental-refresh win.
  std::vector<ActionRecord*> to_scan;
  if (options.scanner) {
    for (ActionRecord& record : *current.mutable_records()) {
      const ActionRecord* reusable = sidecar::FindReusableRecord(stored, record, options.project_id);
      if (reusable == nullptr || CachedScanIsStale(*reusable, directory)) {
        to_scan.push_back(&record);
      }
    }
  }

  // Scan them in parallel, then build the key sets serially. `rescanned` records
  // which keys carry a fresh scan so the merge prefers the current record over
  // the stale cache; `unresolved` records which scans failed.
  const std::vector<std::size_t> failed =
      ParallelScan(to_scan, directory, options.scanner).Run(WorkerCount(options.jobs));
  absl::flat_hash_set<std::string_view> rescanned;
  for (const ActionRecord* record : to_scan) {
    rescanned.insert(record->action_key());
  }
  absl::flat_hash_set<std::string_view> unresolved;
  for (const std::size_t index : failed) {
    unresolved.insert(to_scan[index]->action_key());
  }

  ActionRecords merged = sidecar::MergeRecords(stored, current, options.project_id, rescanned);

  // Stamp written_at on the rows this refresh owns — added, changed, and reused
  // alike — so prune can GC projects that have stopped refreshing. Other
  // projects' rows keep their own timestamps. An action whose scan did not fully
  // resolve is left UNstamped (written_at stays unset, which reads as 0 = stale)
  // so the next refresh re-scans it rather than caching an incomplete header set
  // (CARVE_DESIGN.md sections 4.2, 4.4).
  if (options.now) {
    const std::int64_t stamp = options.now();
    for (ActionRecord& record : *merged.mutable_records()) {
      if (record.project_id() == options.project_id && !unresolved.contains(record.action_key())) {
        record.set_written_at(stamp);
      }
    }
  }

  MBO_RETURN_IF_ERROR(sidecar::Save(options.sidecar_path, merged));

  // Persist the header -> owning-action index alongside the entries sidecar
  // (the design's second cache file). It is a deterministic projection of the
  // merged records, so it is rebuilt from scratch each refresh — the reverse
  // lookup that maps an edited header to the action(s) to re-scan. See
  // CARVE_DESIGN.md sections 4.4-4.5.
  const std::filesystem::path index_path =
      std::filesystem::path(options.sidecar_path).parent_path() / "headers-index.binpb";
  MBO_RETURN_IF_ERROR(sidecar::SaveHeaderIndex(index_path, sidecar::BuildHeaderIndex(merged)));

  const std::vector<cdb::CompileCommand> entries = EntriesFromRecords(merged, directory);
  RefreshStats stats;
  stats.entries = static_cast<int>(entries.size());
  stats.scanned = static_cast<int>(rescanned.size());
  stats.unresolved = static_cast<int>(unresolved.size());
  stats.reused = current.records_size() - stats.scanned;  // Own-project actions not rescanned.
  MBO_RETURN_IF_ERROR(cdb::Write(options.output_path, entries));
  return stats;
}

}  // namespace carve::refresh
