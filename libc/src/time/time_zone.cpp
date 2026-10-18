//===-- POSIX local-time provider -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/time/time_zone.h"
#include "src/__support/CPP/limits.h"
#include "src/time/posix_timezone.h"
#include "src/time/time_utils.h"

#ifdef LIBC_COPT_POSIX_TIMEZONE
#include "hdr/func/malloc.h"
#include "src/__support/CPP/mutex.h"
#include "src/__support/CPP/new.h"
#include "src/__support/threads/mutex.h"
#include "src/time/tz_variables.h"
#ifdef LIBC_FULL_BUILD
#include "config/app.h"
#include "src/stdlib/environ_internal.h"
#else
#include "hdr/stdlib_overlay.h"
#endif
#endif

namespace LIBC_NAMESPACE_DECL {

#ifdef LIBC_COPT_POSIX_TIMEZONE
namespace {
char utc_name[] = "UTC";
char empty_name[] = "";
} // namespace
#endif

namespace time_utils {
namespace {
struct ZoneSnapshot {
  PosixTimeZone rules;
  const char *names[2] = {"UTC", ""};
};

#ifdef LIBC_COPT_POSIX_TIMEZONE
Mutex zone_mutex(false, false, false, false);

// Abbreviations may escape via tm_zone. Intern each distinct spelling once;
// never free or mutate it, even after tzset, so readers need no lifetime lock.
struct ZoneName {
  ZoneName *next;
  size_t length;
  char *data() { return reinterpret_cast<char *>(this + 1); }
};
ZoneName *zone_names = nullptr;

const char *intern(cpp::string_view name) {
  if (name.empty())
    return empty_name;
  if (name == "UTC")
    return utc_name;
  for (ZoneName *node = zone_names; node; node = node->next)
    if (cpp::string_view(node->data(), node->length) == name)
      return node->data();
  if (name.size() > cpp::numeric_limits<size_t>::max() - sizeof(ZoneName) - 1)
    return nullptr;
  void *memory = ::malloc(sizeof(ZoneName) + name.size() + 1);
  if (!memory)
    return nullptr;
  auto *node = new (memory) ZoneName{zone_names, name.size()};
  for (size_t i = 0; i < name.size(); ++i)
    node->data()[i] = name[i];
  node->data()[name.size()] = '\0';
  zone_names = node;
  return node->data();
}

ErrorOr<ZoneSnapshot> snapshot() {
  cpp::lock_guard guard(zone_mutex);
#ifdef LIBC_FULL_BUILD
  // A full-build function can also be linked into a native unit-test process
  // without our startup object. There is no libc-owned environment in that
  // case.
  const char *value =
      &app ? internal::EnvironmentManager::get_instance().get("TZ") : nullptr;
#else
  const char *value = ::getenv("TZ");
#endif
  // No system zone database is consulted. Only an absent/empty TZ defaults.
  if (!value || !*value)
    value = "UTC0";
  // These built-in zero-offset names do not require a zoneinfo database.
  else if (cpp::string_view(value) == "GMT")
    value = "GMT0";
  else if (cpp::string_view(value) == "UTC")
    value = "UTC0";
  ZoneSnapshot zone;
  auto status = parse_posix_timezone(value, zone.rules);
  if (status != TZParseResult::SUCCESS) {
#ifdef LIBC_COPT_TZ_VARIABLES
    tzname[0] = tzname[1] = empty_name;
    timezone = 0;
    daylight = 0;
#endif
    return cpp::unexpected(status == TZParseResult::INVALID ? EINVAL : ENOTSUP);
  }
  for (int i = 0; i < 2; ++i) {
    zone.names[i] = intern(zone.rules.names[i]);
    if (!zone.names[i])
      return cpp::unexpected(ENOMEM);
    zone.rules.names[i] = zone.names[i];
  }
#ifdef LIBC_COPT_TZ_VARIABLES
  tzname[0] = const_cast<char *>(zone.names[0]);
  tzname[1] = const_cast<char *>(zone.names[1]);
  timezone = -zone.rules.offsets[0];
  daylight = zone.rules.has_dst;
#endif
  return zone;
}
#else
// Non-POSIX targets retain their UTC-only local-time implementation.
ErrorOr<ZoneSnapshot> snapshot() { return ZoneSnapshot{}; }
#endif

ErrorOr<time_t> shift_seconds(int64_t seconds, int offset) {
  time_t shifted;
  if (__builtin_add_overflow(seconds, offset, &shifted))
    return cpp::unexpected(TIME_OVERFLOW);
  return shifted;
}

void set_metadata(tm *result, const ZoneSnapshot &zone, bool is_dst) {
  result->tm_isdst = is_dst;
#if defined(__linux__)
  result->tm_gmtoff = zone.rules.offsets[is_dst];
  result->tm_zone = zone.names[is_dst];
#else
  (void)zone;
#endif
}

ErrorOr<tm *> convert(time_t seconds, tm *result, const ZoneSnapshot &zone) {
  // Determine the UTC year before applying either offset; transition clocks
  // can lie in adjacent years, and must not be interpreted in the new offset.
  int64_t days = seconds / 86400 - (seconds % 86400 < 0);
  int64_t utc_year = civil_date(days).year;
  // Leave one year of headroom for an offset crossing the tm_year boundary.
  // This also bounds transition arithmetic well within int64_t.
  if (utc_year < int64_t(cpp::numeric_limits<int>::min()) + 1899 ||
      utc_year > int64_t(cpp::numeric_limits<int>::max()) + 1901)
    return cpp::unexpected(TIME_OVERFLOW);
  bool is_dst = zone.rules.is_dst(seconds, utc_year);
  int64_t shifted;
  if (__builtin_add_overflow(seconds, zone.rules.offsets[is_dst], &shifted))
    return cpp::unexpected(TIME_OVERFLOW);
  tm local;
  auto status = update_from_seconds(shifted, &local);
  if (!status)
    return cpp::unexpected(status.error());
  set_metadata(&local, zone, is_dst);
  *result = local;
  return result;
}

ErrorOr<time_t> invert(tm *result, const ZoneSnapshot &zone) {
  int64_t wall = civil_seconds(result);
  auto standard = shift_seconds(wall, -zone.rules.offsets[0]);
  auto summer = shift_seconds(wall, -zone.rules.offsets[1]);
  bool explicit_dst = result->tm_isdst > 0 && zone.rules.has_dst;
  auto chosen = explicit_dst ? summer : standard;
  if (result->tm_isdst < 0 && zone.rules.has_dst) {
    tm candidate;
    // Prefer standard time in a fold; otherwise accept a matching DST
    // candidate. In a gap choose the later candidate (normalize forwards),
    // including zones whose daylight offset is smaller than standard time.
    bool standard_matches = standard && convert(*standard, &candidate, zone) &&
                            candidate.tm_isdst == 0;
    bool summer_matches =
        summer && convert(*summer, &candidate, zone) && candidate.tm_isdst == 1;
    if (standard_matches)
      chosen = standard;
    else if (summer_matches)
      chosen = summer;
    else if (standard && summer)
      chosen = *standard > *summer ? standard : summer;
  }
  if (!chosen)
    return cpp::unexpected(chosen.error());
  auto status = convert(*chosen, result, zone);
  if (!status)
    return cpp::unexpected(status.error());
  return *chosen;
}
} // namespace

ErrorOr<void> refresh_timezone() {
  auto zone = snapshot();
  if (!zone)
    return cpp::unexpected(zone.error());
  return {};
}

ErrorOr<tm *> localtime_internal(const time_t *timer, tm *result) {
  auto zone = snapshot();
  if (!zone)
    return cpp::unexpected(zone.error());
  return convert(*timer, result, *zone);
}

ErrorOr<time_t> mktime_local(tm *result) {
  auto zone = snapshot();
  if (!zone)
    return cpp::unexpected(zone.error());
  return invert(result, *zone);
}

ErrorOr<void> prepare_timezone_for_format(tm *result, bool epoch) {
#if defined(__linux__)
  if (result->tm_zone)
    return {};
#endif
  if (!epoch && result->tm_isdst < 0)
    return {};
  auto zone = snapshot();
  if (!zone)
    return cpp::unexpected(zone.error());
  if (epoch) {
    auto seconds = invert(result, *zone);
    if (!seconds)
      return cpp::unexpected(seconds.error());
  } else {
    set_metadata(result, *zone, result->tm_isdst > 0 && zone->rules.has_dst);
  }
  return {};
}

} // namespace time_utils
} // namespace LIBC_NAMESPACE_DECL
