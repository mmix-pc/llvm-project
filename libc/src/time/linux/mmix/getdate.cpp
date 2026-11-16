//===-- MMIX template-based date conversion -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/time/getdate.h"
#include "hdr/fcntl_macros.h"
#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/stdlib/getenv.h"
#include "src/time/linux/mmix/getdate_zone.h"
#include "src/time/posix_timezone.h"
#include "src/time/strptime_parser.h"
#include "src/time/time_func.h"
#include "src/time/time_utils.h"
#include "src/time/time_zone.h"
#include <sys/syscall.h>
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_VARIABLE(int, getdate_err) = 0;
namespace {
tm result_storage;
tm *fail(int error) {
  getdate_err = error;
  return nullptr;
}
bool space(char c) {
  return internal::classify_byte(static_cast<unsigned char>(c),
                                 internal::WCTYPE_SPACE,
                                 internal::current_ctype());
}
ErrorOr<time_t> interpret(tm &candidate, const tm &fields,
                          const time_utils::GetdateZone *zone = nullptr) {
  const tm initial = candidate;
  candidate.tm_isdst = fields.tm_isdst;
  bool date = fields.tm_year != cpp::numeric_limits<int>::min() ||
              fields.tm_mon != -1 || fields.tm_mday != -1 ||
              fields.tm_wday != -1;
  if (fields.tm_year != cpp::numeric_limits<int>::min())
    candidate.tm_year = fields.tm_year;
  if (fields.tm_mon != -1) {
    candidate.tm_mon = fields.tm_mon;
    if (fields.tm_year == cpp::numeric_limits<int>::min() &&
        fields.tm_mon < initial.tm_mon) {
      if (candidate.tm_year == cpp::numeric_limits<int>::max())
        return cpp::unexpected(EINVAL);
      ++candidate.tm_year;
    }
    if (fields.tm_mday == -1)
      candidate.tm_mday = 1;
  }
  if (fields.tm_mday != -1)
    candidate.tm_mday = fields.tm_mday;
  if (fields.tm_hour != -1)
    candidate.tm_hour = fields.tm_hour;
  if (fields.tm_min != -1)
    candidate.tm_min = fields.tm_min;
  if (fields.tm_sec != -1)
    candidate.tm_sec = fields.tm_sec;
  int month = candidate.tm_mon, day = candidate.tm_mday,
      year = candidate.tm_year;
  int64_t first = time_utils::civil_days(int64_t(year) + 1900, month + 1, 1);
  int64_t next =
      month == 11 ? time_utils::civil_days(int64_t(year) + 1901, 1, 1)
                  : time_utils::civil_days(int64_t(year) + 1900, month + 2, 1);
  if (day < 1 || day > next - first)
    return cpp::unexpected(EINVAL);
  if (fields.tm_wday != -1 &&
      fields.tm_year == cpp::numeric_limits<int>::min() &&
      fields.tm_mon == -1 && fields.tm_mday == -1)
    candidate.tm_mday += (fields.tm_wday - initial.tm_wday + 7) % 7;
  if (!date && fields.tm_hour != -1 && candidate.tm_hour < initial.tm_hour)
    ++candidate.tm_mday;
  time_t epoch;
  if (zone) {
    int64_t wall = time_utils::civil_seconds(&candidate);
    if (__builtin_sub_overflow(wall, zone->offset, &epoch))
      return cpp::unexpected(EOVERFLOW);
    tm actual;
    auto converted = time_utils::localtime_internal(&epoch, &actual);
    if (!converted)
      return cpp::unexpected(converted.error());
    if (actual.tm_gmtoff != zone->offset ||
        (actual.tm_isdst > 0) != zone->daylight ||
        time_utils::civil_seconds(&actual) != wall)
      return cpp::unexpected(EINVAL);
    candidate = actual;
  } else {
    auto converted = time_utils::mktime_local(&candidate);
    if (!converted)
      return cpp::unexpected(converted.error());
    epoch = *converted;
  }
  if (fields.tm_wday != -1 && candidate.tm_wday != fields.tm_wday)
    return cpp::unexpected(EINVAL);
  return epoch;
}
} // namespace
LLVM_LIBC_FUNCTION(tm *, getdate, (const char *text)) {
  const char *path = LIBC_NAMESPACE::getenv("DATEMSK");
  if (!path || !*path)
    return fail(1);
  long fd = syscall_impl<long>(SYS_openat, AT_FDCWD, path,
                               O_RDONLY | O_CLOEXEC | O_NONBLOCK, 0);
  if (fd < 0)
    return fail(2);
  struct Close {
    long fd;
    ~Close() { syscall_impl<long>(SYS_close, fd); }
  } owner{fd};
  internal::kernel_statx_buf st;
  auto status =
      linux_syscalls::statx(static_cast<int>(fd), "", AT_EMPTY_PATH,
                            internal::KERNEL_STATX_BASIC_STATS_MASK, &st);
  if (!status)
    return fail(3);
  if ((st.stx_mode & 0170000) != 0100000)
    return fail(4);
  if (st.stx_size >= cpp::numeric_limits<size_t>::max())
    return fail(6);
  size_t size = static_cast<size_t>(st.stx_size);
  char *formats = static_cast<char *>(::malloc(size + 1));
  if (!formats)
    return fail(6);
  struct Free {
    char *p;
    ~Free() { ::free(p); }
  } memory{formats};
  size_t used = 0;
  for (;;) {
    // st_size is a sizing hint, not EOF (for example, procfs regular files
    // commonly report zero). Probe before growing an exactly filled buffer.
    char probe;
    bool full = used == size;
    long n = syscall_impl<long>(SYS_read, fd, full ? &probe : formats + used,
                                full ? size_t(1) : size - used);
    if (n == -EINTR)
      continue;
    if (n < 0)
      return fail(5);
    if (!n)
      break;
    if (full) {
      size_t capacity;
      if (__builtin_mul_overflow(size, size_t(2), &capacity) ||
          capacity == cpp::numeric_limits<size_t>::max())
        return fail(6);
      if (capacity < 256)
        capacity = 256;
      char *larger = static_cast<char *>(::malloc(capacity + 1));
      if (!larger)
        return fail(6);
      for (size_t i = 0; i < used; ++i)
        larger[i] = formats[i];
      ::free(formats);
      formats = memory.p = larger;
      size = capacity;
      formats[used] = probe;
    }
    used += static_cast<size_t>(n);
  }
  formats[used] = 0;
  time_t now = LIBC_NAMESPACE::time(nullptr);
  tm current;
  auto initial_time = time_utils::localtime_internal(&now, &current);
  if (!initial_time)
    return fail(initial_time.error() == ENOMEM ? 6 : 8);
  char *line = formats;
  while (line < formats + used) {
    char *end = line;
    while (*end && *end != '\n')
      ++end;
    bool more = *end == '\n';
    *end = 0;
    internal::StrptimeParser parser(true);
    const char *input = text;
    size_t available = cpp::numeric_limits<size_t>::max();
    bool parsed = parser.parse(input, line, available) && parser.finish();
    while (*input && space(*input))
      ++input;
    if (parsed && !*input) {
      tm fields{};
      fields.tm_year = cpp::numeric_limits<int>::min();
      fields.tm_mon = fields.tm_mday = fields.tm_wday = fields.tm_yday = -1;
      fields.tm_hour = fields.tm_min = fields.tm_sec = fields.tm_isdst = -1;
      parser.commit(fields);
      tm candidate = current;
      if (parser.has_timezone()) {
        bool found = false;
        time_t best_epoch = 0;
        tm best{};
        for (size_t index = 0;; ++index) {
          auto entry = time_utils::getdate_timezone_type(index);
          if (!entry)
            return fail(entry.error() == ENOMEM ? 6 : 8);
          if (!*entry)
            break;
          const auto &zone = **entry;
          if (!parser.matches_timezone(zone.name))
            continue;
          int64_t local_now;
          if (__builtin_add_overflow(int64_t(now), zone.offset, &local_now) ||
              !time_utils::update_from_seconds(local_now, &candidate))
            continue;
          auto epoch = interpret(candidate, fields, &zone);
          if (!epoch) {
            if (epoch.error() == ENOMEM)
              return fail(6);
            continue;
          }
          if (!parser.matches_timezone(candidate.tm_zone))
            continue;
          if (!found || (best.tm_isdst > 0 && candidate.tm_isdst == 0) ||
              (best.tm_isdst == candidate.tm_isdst && *epoch > best_epoch)) {
            found = true;
            best = candidate;
            best_epoch = *epoch;
          }
        }
        if (!found)
          return fail(8);
        candidate = best;
      } else {
        auto epoch = interpret(candidate, fields);
        if (!epoch)
          return fail(epoch.error() == ENOMEM ? 6 : 8);
      }
      result_storage = candidate;
      return &result_storage;
    }
    if (!more)
      break;
    line = end + 1;
  }
  return fail(7);
}
} // namespace LIBC_NAMESPACE_DECL
