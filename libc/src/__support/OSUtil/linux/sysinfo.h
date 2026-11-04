//===------------- Linux sysinfo support -------------------------------------//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_SYSINFO_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_SYSINFO_H

#include "hdr/errno_macros.h"
#include "hdr/limits_macros.h"
#include "src/__support/CPP/array.h"
#include "src/__support/CPP/bit.h"
#include "src/__support/CPP/optional.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/read.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/sched_getaffinity.h"
#include "src/__support/ctype_utils.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace sysinfo {

LIBC_INLINE_VAR constexpr char POSSIBLE_NPROC_PATH[] =
    "/sys/devices/system/cpu/possible";
LIBC_INLINE_VAR constexpr char ONLINE_NPROC_PATH[] =
    "/sys/devices/system/cpu/online";

// Parses Linux CPU-list syntax:
//   list  := item (',' item)* ['\n']
//   item  := number | number '-' number
//   number := [0-9]+
// Kernel lists are ordered and disjoint. Reject malformed lists rather than
// publishing a partial, wrapped or double-counted configured CPU count.
class ProcParser {
  enum class ProcParserState {
    ParseUnstarted,
    ParseNumber,
    ParseRangeSeparator,
    ParseRangeEnd,
    ParseEnded
  };

  ProcParserState state;
  cpp::array<char, 128> buffer;
  int fd;
  size_t cursor;
  size_t buffer_end;
  size_t cpu_count;
  size_t current_number;
  size_t range_start;
  size_t previous_end;
  int error;

  LIBC_INLINE cpp::optional<char> next_char() {
    if (fd < 0)
      return cpp::nullopt;

    while (cursor == buffer_end) {
      ErrorOr<ssize_t> bytes_read =
          linux_syscalls::read(fd, buffer.data(), buffer.size());
      if (!bytes_read) {
        if (bytes_read.error() == EINTR)
          continue;
        error = bytes_read.error();
        return cpp::nullopt;
      }

      if (*bytes_read == 0)
        return cpp::nullopt;

      cursor = 0;
      buffer_end = static_cast<size_t>(*bytes_read);
    }

    return buffer[cursor++];
  }

  LIBC_INLINE bool finish_group() {
    if (state != ProcParserState::ParseNumber &&
        state != ProcParserState::ParseRangeEnd)
      return false;

    size_t start = current_number;
    if (state == ProcParserState::ParseRangeEnd) {
      if (current_number < range_start)
        return false;
      start = range_start;
    }
    if (cpu_count != 0 && start <= previous_end)
      return false;
    size_t count = current_number - start + 1;
    if (count > static_cast<size_t>(LONG_MAX) - cpu_count) {
      error = EOVERFLOW;
      return false;
    }
    cpu_count += count;
    previous_end = current_number;

    current_number = 0;
    range_start = 0;
    state = ProcParserState::ParseUnstarted;
    return true;
  }

  LIBC_INLINE bool consume(char ch) {
    if (state == ProcParserState::ParseEnded)
      return false;
    if (internal::isdigit(ch)) {
      // Not using internal::strtointeger here because a number can be across
      // two reads in rare cases.
      size_t digit = static_cast<size_t>(ch - '0');
      if (current_number > (static_cast<size_t>(LONG_MAX) - digit) / 10) {
        error = EOVERFLOW;
        return false;
      }
      current_number = current_number * 10 + digit;
      if (state == ProcParserState::ParseUnstarted)
        state = ProcParserState::ParseNumber;
      else if (state == ProcParserState::ParseRangeSeparator)
        state = ProcParserState::ParseRangeEnd;
      return true;
    }

    if (ch == '-') {
      if (state != ProcParserState::ParseNumber)
        return false;
      range_start = current_number;
      current_number = 0;
      state = ProcParserState::ParseRangeSeparator;
      return true;
    }

    if (ch == ',' || ch == '\n') {
      if (!finish_group())
        return false;
      if (ch == '\n')
        state = ProcParserState::ParseEnded;
      return true;
    }

    return false;
  }

public:
  // The path must be null-terminated.
  LIBC_INLINE explicit ProcParser(const char *path)
      : state(ProcParserState::ParseUnstarted), buffer{}, fd(-1),
        cursor(buffer.size()), buffer_end(buffer.size()), cpu_count(0),
        current_number(0), range_start(0), previous_end(0), error(0) {
    for (;;) {
      ErrorOr<int> result =
          linux_syscalls::open(path, O_RDONLY | O_CLOEXEC, 0);
      if (result) {
        fd = *result;
        break;
      }
      if (result.error() != EINTR) {
        error = result.error();
        break;
      }
    }
  }

  LIBC_INLINE ~ProcParser() {
    if (fd >= 0)
      linux_syscalls::close(fd);
  }

  LIBC_INLINE ErrorOr<size_t> parse() {
    if (fd < 0)
      return Error(error);

    while (cpp::optional<char> ch = next_char())
      if (!consume(*ch))
        return Error(error ? error : EIO);

    if (error)
      return Error(error);

    if (state != ProcParserState::ParseEnded && !finish_group())
      return Error(error ? error : EIO);

    if (cpu_count == 0)
      return Error(EIO);
    return cpu_count;
  }
};

LIBC_INLINE ErrorOr<size_t> parse_nproc_from(const char *path) {
  return ProcParser(path).parse();
}

LIBC_INLINE size_t parse_nproc_with_fallback_from(const char *path) {
  if (ErrorOr<size_t> cpu_count = parse_nproc_from(path))
    return *cpu_count;

  cpp::array<unsigned char, 128> mask_buffer = {};

  ErrorOr<int> affinity_result =
      linux_syscalls::sched_getaffinity(0, mask_buffer);
  if (!affinity_result)
    return 1;

  size_t cpu_count = 0;
  for (unsigned char byte : mask_buffer)
    cpu_count += static_cast<size_t>(cpp::popcount(byte));

  return cpu_count > 0 ? cpu_count : 1;
}

} // namespace sysinfo
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_SYSINFO_H
