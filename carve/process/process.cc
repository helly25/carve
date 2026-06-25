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

#include "carve/process/process.h"

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace carve::process {
namespace {

// A process terminated by signal N is reported as 128 + N (shell convention).
constexpr int kSignalExitBase = 128;
// Exit status used when the child's exec fails (e.g. program not found); 127 is
// the shell convention for "command not found".
constexpr int kExecFailedExitCode = 127;
// Read chunk size when draining the child's pipes.
constexpr std::size_t kReadChunkBytes = 4'096;

// Builds a status from the current `errno`. Uses `absl::ErrnoToStatus`, which
// formats the message with a thread-safe `strerror` internally and maps the
// errno to the matching `absl::StatusCode` (`std::strerror` is not thread-safe).
absl::Status ErrnoError(std::string_view what) {
  return absl::ErrnoToStatus(errno, what);
}

// Drains `out_fd` and `err_fd` into the result strings until both reach EOF,
// polling so neither pipe blocks the other.
void DrainPipes(int out_fd, int err_fd, CommandResult& result) {
  std::array<pollfd, 2> fds = {
      pollfd{.fd = out_fd, .events = POLLIN, .revents = 0}, pollfd{.fd = err_fd, .events = POLLIN, .revents = 0}};
  std::array<std::string*, 2> sinks = {&result.stdout_data, &result.stderr_data};
  int open_fds = 2;
  while (open_fds > 0) {
    if (::poll(fds.data(), fds.size(), -1) < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    for (std::size_t i = 0; i < fds.size(); ++i) {
      // POSIX poll flags are signed int macros; the bitmask test is the idiomatic form.
      // NOLINTNEXTLINE(hicpp-signed-bitwise)
      if (fds[i].fd < 0 || (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
        continue;
      }
      std::array<char, kReadChunkBytes> buffer{};
      const ssize_t got = ::read(fds[i].fd, buffer.data(), buffer.size());
      if (got > 0) {
        sinks[i]->append(buffer.data(), static_cast<std::size_t>(got));
      } else if (got == 0 || errno != EINTR) {
        ::close(fds[i].fd);
        fds[i].fd = -1;
        --open_fds;
      }
    }
  }
  // Close anything still open (e.g. if poll() failed and broke the loop).
  for (const pollfd& entry : fds) {
    if (entry.fd >= 0) {
      ::close(entry.fd);
    }
  }
}

}  // namespace

absl::StatusOr<CommandResult> Run(absl::Span<const std::string> argv) {
  if (argv.empty()) {
    return absl::InvalidArgumentError("Run requires a non-empty argv");
  }

  std::array<int, 2> out_pipe{};
  std::array<int, 2> err_pipe{};
  // macOS has no pipe2(); the child closes every pipe fd before exec (below), so none leak.
  // NOLINTNEXTLINE(android-cloexec-pipe)
  if (::pipe(out_pipe.data()) != 0) {
    return ErrnoError("pipe(stdout)");
  }
  // NOLINTNEXTLINE(android-cloexec-pipe)
  if (::pipe(err_pipe.data()) != 0) {
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    return ErrnoError("pipe(stderr)");
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    for (const int descriptor : {out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]}) {
      ::close(descriptor);
    }
    return ErrnoError("fork");
  }

  if (pid == 0) {
    // Child: wire stdout/stderr to the pipes and exec.
    ::dup2(out_pipe[1], STDOUT_FILENO);
    ::dup2(err_pipe[1], STDERR_FILENO);
    for (const int descriptor : {out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]}) {
      ::close(descriptor);
    }
    std::vector<char*> c_argv;
    c_argv.reserve(argv.size() + 1);
    for (const std::string& arg : argv) {
      // execvp wants char* const[]; argv outlives the call and is not modified.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
      c_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    c_argv.push_back(nullptr);
    ::execvp(c_argv[0], c_argv.data());
    ::_exit(kExecFailedExitCode);  // Reached only if exec failed (e.g. program not found).
  }

  // Parent: close write ends, drain, and reap.
  ::close(out_pipe[1]);
  ::close(err_pipe[1]);
  CommandResult result;
  DrainPipes(out_pipe[0], err_pipe[0], result);

  int status = 0;
  while (::waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      return ErrnoError("waitpid");
    }
  }
  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = kSignalExitBase + WTERMSIG(status);
  }
  return result;
}

}  // namespace carve::process
