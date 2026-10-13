//===-- Loader bootstrap resource tests -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/arena.h"
#include "loader/linux/io.h"
#include "loader/linux/syscall.h"
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
long results[8];
unsigned calls, result_count, maps, unmaps, closes;
long addresses[8], lengths[8];
bool fail_unmap, fatal_mode, return_from_exit;
long map_error;
unsigned fatal_writes;
size_t fatal_progress;
int report_fd;
long fatal_write_result = -EINTR;
constexpr char diagnostic[] = "LLVM loader: startup failure\n";

void script(long a, long b = 0, long c = 0) {
  results[0] = a;
  results[1] = b;
  results[2] = c;
  result_count = 3;
  calls = 0;
}
} // namespace

namespace LIBC_NAMESPACE {
namespace loader {
// Link-time injection replaces only the target syscall adapter. The I/O and
// arena algorithms below are the production sources, without test branches.
long raw_syscall(long number, long a1, long a2, long a3, long a4, long a5,
                 long a6) {
  if (fatal_mode) {
    if (number == SYS_write) {
      if (a1 != 2 ||
          a3 != static_cast<long>(sizeof(diagnostic) - 1 - fatal_progress))
        _exit(90);
      auto *text = reinterpret_cast<const char *>(a2);
      for (long i = 0; i < a3; ++i)
        if (text[i] != diagnostic[fatal_progress + static_cast<size_t>(i)])
          _exit(95);
      ++fatal_writes;
      if (fatal_write_result > 0 && fatal_write_result <= a3)
        fatal_progress += static_cast<size_t>(fatal_write_result);
      return fatal_write_result;
    }
    if (number == SYS_exit_group) {
      unsigned report[] = {fatal_writes, static_cast<unsigned>(a1)};
      if (::write(report_fd, report, sizeof(report)) != sizeof(report))
        _exit(91);
      if (return_from_exit)
        return 0;
      _exit(static_cast<int>(a1));
    }
    _exit(92);
  }
  if (number == SYS_mmap) {
    if (a1 != 0 || a2 <= 0 || a3 != (PROT_READ | PROT_WRITE) ||
        a4 != (MAP_PRIVATE | MAP_ANONYMOUS) || a5 != -1 || a6 != 0)
      _exit(93);
    if (map_error)
      return map_error;
    void *p = ::mmap(nullptr, static_cast<size_t>(a2), static_cast<int>(a3),
                     static_cast<int>(a4), -1, 0);
    if (p == MAP_FAILED)
      return -errno;
    ++maps;
    return reinterpret_cast<long>(p);
  }
  if (number == SYS_munmap) {
    if (fail_unmap) {
      fail_unmap = false;
      return -EIO;
    }
    if (::munmap(reinterpret_cast<void *>(a1), static_cast<size_t>(a2)) != 0)
      return -errno;
    ++unmaps;
    return 0;
  }
  if (number == SYS_close)
    ++closes;
  if (calls >= result_count)
    _exit(94);
  addresses[calls] = a2;
  lengths[calls] = a3;
  return results[calls++];
}
} // namespace loader
} // namespace LIBC_NAMESPACE

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr))                                                               \
      return __LINE__ % 255 + 1;                                               \
  } while (false)

using namespace LIBC_NAMESPACE::loader;

static int test_io() {
  char buffer[8];
  errno = EDOM;
  script(-EINTR, 3, 5);
  auto r = read_bytes(42, buffer, sizeof(buffer));
  CHECK(r.error == 0 && r.transferred == 8 && calls == 3);
  CHECK(addresses[0] == addresses[1] && addresses[2] == addresses[1] + 3);
  CHECK(lengths[0] == 8 && lengths[1] == 8 && lengths[2] == 5);
  script(3, 0);
  r = read_bytes(42, buffer, sizeof(buffer));
  CHECK(r.error == 0 && r.transferred == 3 && calls == 2);
  script(3, -EINTR, -EAGAIN);
  r = read_bytes(42, buffer, sizeof(buffer));
  CHECK(r.error == -EAGAIN && r.transferred == 3 && calls == 3);
  script(3, -EIO);
  r = write_bytes(42, buffer, sizeof(buffer));
  CHECK(r.error == -EIO && r.transferred == 3 && calls == 2);
  CHECK(addresses[1] == addresses[0] + 3 && lengths[1] == 5);
  script(-EINTR, 4, 4);
  r = write_bytes(42, buffer, sizeof(buffer));
  CHECK(r.error == 0 && r.transferred == 8 && calls == 3);
  script(0);
  r = write_bytes(42, buffer, sizeof(buffer));
  CHECK(r.error == -EIO && r.transferred == 0 && calls == 1);
  script(9);
  CHECK(read_bytes(42, buffer, 8).error == -EIO);
  script(0);
  CHECK(read_bytes(42, buffer, 0).transferred == 0 && calls == 0);
  CHECK(read_bytes(42, buffer, static_cast<size_t>(-1)).error == -EOVERFLOW);
  CHECK(read_bytes(42, reinterpret_cast<void *>(__LONG_MAX__), 2).error ==
        -EOVERFLOW);
  CHECK(calls == 0 && errno == EDOM);
  return 0;
}

static int test_fd() {
  {
    OwnedFd fd;
    script(-EACCES);
    CHECK(fd.open_readonly("missing") == -EACCES && fd.get() == -1);
    script(-EINTR, 42);
    CHECK(fd.open_readonly("input") == 0 && fd.get() == 42 && calls == 2);
    CHECK(fd.open_readonly("other") == -EBUSY && calls == 2);
    script(-EINTR);
    CHECK(fd.close() == -EINTR && fd.get() == -1 && closes == 1);
    CHECK(fd.close() == 0 && closes == 1);
    // Reuse the consumed descriptor number. The old owner must not close it.
    OwnedFd reused;
    script(42);
    CHECK(reused.open_readonly("new") == 0);
    script(0);
  }
  CHECK(closes == 2);
  return 0;
}

static int test_arena() {
  size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
  Arena arena(page);
  void *a, *b;
  CHECK(arena.allocate(32, 16, a) == 0);
  CHECK(reinterpret_cast<unsigned long>(a) % 16 == 0);
  CHECK(arena.allocate(48, 32, b) == 0 && maps == 1);
  CHECK(reinterpret_cast<unsigned long>(b) >=
        reinterpret_cast<unsigned long>(a) + 32);
  CHECK(reinterpret_cast<unsigned long>(b) % 32 == 0);
  static_cast<char *>(a)[31] = 7;
  CHECK(arena.allocate(page, page, b) == 0 && maps == 2);
  CHECK(reinterpret_cast<unsigned long>(b) % page == 0);
  CHECK(static_cast<char *>(a)[31] == 7);
  CHECK(arena.allocate(0, 1, b) == -EINVAL && b == nullptr);
  CHECK(arena.allocate(8, 3, b) == -EINVAL);
  CHECK(arena.allocate(8, page * 2, b) == -EINVAL);
  CHECK(arena.allocate(static_cast<size_t>(-1), 1, b) == -EOVERFLOW);
  CHECK(arena.allocate(__LONG_MAX__ - 32, page, b) == -EOVERFLOW);
  CHECK(maps == 2);
  map_error = -ENOMEM;
  CHECK(arena.allocate(page * 4, 8, b) == -ENOMEM && b == nullptr);
  map_error = -EINTR;
  CHECK(arena.allocate(page * 4, 8, b) == -EINTR && b == nullptr);
  map_error = 0;
  fail_unmap = true;
  CHECK(arena.reset() == -EIO && unmaps == 1);
  CHECK(arena.reset() == 0 && unmaps == 2);
  CHECK(arena.reset() == 0 && unmaps == 2);
  CHECK(arena.allocate(16, 8, b) == 0 && maps == 3);
  CHECK(arena.reset() == 0 && unmaps == 3);
  Arena invalid(3);
  CHECK(invalid.allocate(16, 8, b) == -EINVAL && maps == 3);
  return 0;
}

static int test_fatal(bool trap, long write_result, unsigned expected_writes) {
  int pipefd[2];
  CHECK(::pipe(pipefd) == 0);
  pid_t child = ::fork();
  CHECK(child >= 0);
  if (child == 0) {
    ::close(pipefd[0]);
    report_fd = pipefd[1];
    fatal_mode = true;
    return_from_exit = trap;
    fatal_write_result = write_result;
    fatal_startup();
  }
  ::close(pipefd[1]);
  unsigned report[2];
  CHECK(::read(pipefd[0], report, sizeof(report)) == sizeof(report));
  ::close(pipefd[0]);
  int status;
  CHECK(::waitpid(child, &status, 0) == child);
  CHECK(report[0] == expected_writes && report[1] == 127);
  if (trap)
    CHECK(WIFSIGNALED(status) &&
          (WTERMSIG(status) == SIGILL || WTERMSIG(status) == SIGTRAP));
  else
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 127);
  return 0;
}

extern "C" int main() {
  int error = test_io();
  if (!error)
    error = test_fd();
  if (!error)
    error = test_arena();
  if (!error)
    error = test_fatal(false, -EINTR, 8);
  if (!error)
    error = test_fatal(false, -EIO, 1);
  if (!error)
    error = test_fatal(false, 1, 8);
  if (!error)
    error = test_fatal(false, sizeof(diagnostic) - 1, 1);
  if (!error)
    error = test_fatal(true, -EINTR, 8);
  return error;
}
