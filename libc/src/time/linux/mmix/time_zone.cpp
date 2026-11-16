//===-- POSIX local-time provider -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/time/time_zone.h"
#include "hdr/fcntl_macros.h"
#include "hdr/func/free.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/time/linux/mmix/getdate_zone.h"
#include "src/time/linux/mmix/tzif.h"
#include "src/time/posix_timezone.h"
#include "src/time/time_utils.h"
#include <sys/syscall.h>

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
  TZifView file;
  bool geographical = false, default_rules = false;
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

ZoneSnapshot current_zone;
char *current_key = nullptr;
unsigned char *current_data = nullptr;
bool initialized = false, current_absent = true;

bool same(const char *a, const char *b) {
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}
size_t length(const char *p) {
  size_t n = 0;
  while (p[n])
    ++n;
  return n;
}

struct FileData {
  unsigned char *bytes = nullptr;
  ~FileData() { ::free(bytes); }
  // Non-cancelling private I/O under the timezone mutex. No partial snapshot
  // is published; errno is not used as the private error transport.
  ErrorOr<TZifView> load(const char *path) {
    long fd = syscall_impl<long>(SYS_openat, AT_FDCWD, path,
                                 O_RDONLY | O_CLOEXEC | O_NONBLOCK, 0);
    if (fd < 0)
      return cpp::unexpected(int(-fd));
    struct Close {
      long fd;
      ~Close() { syscall_impl<long>(SYS_close, fd); }
    } owner{fd};
    internal::kernel_statx_buf st;
    auto status =
        linux_syscalls::statx(static_cast<int>(fd), "", AT_EMPTY_PATH,
                              internal::KERNEL_STATX_BASIC_STATS_MASK, &st);
    if (!status)
      return cpp::unexpected(status.error());
    if ((st.stx_mode & 0170000) != 0100000 || st.stx_size < 44 ||
        st.stx_size > cpp::numeric_limits<size_t>::max())
      return cpp::unexpected(EINVAL);
    size_t size = static_cast<size_t>(st.stx_size);
    bytes = static_cast<unsigned char *>(::malloc(size));
    if (!bytes)
      return cpp::unexpected(ENOMEM);
    size_t done = 0;
    while (done < size) {
      long n = syscall_impl<long>(SYS_read, fd, bytes + done, size - done);
      if (n == -EINTR)
        continue;
      if (n <= 0)
        return cpp::unexpected(n < 0 ? int(-n) : EINVAL);
      done += static_cast<size_t>(n);
    }
    // Reject growth as well as truncation rather than silently parsing a
    // prefix.
    unsigned char extra;
    long n;
    do {
      n = syscall_impl<long>(SYS_read, fd, &extra, 1);
    } while (n == -EINTR);
    if (n != 0)
      return cpp::unexpected(n < 0 ? int(-n) : EINVAL);
    TZifView file;
    if (TZifView::parse(bytes, size, file) != TZifView::Result::SUCCESS)
      return cpp::unexpected(EINVAL);
    return file;
  }
};
bool resource_error(int e) { return e == ENOMEM || e == EMFILE || e == ENFILE; }

ErrorOr<ZoneSnapshot> snapshot(bool force = false) {
#ifdef LIBC_FULL_BUILD
  const char *value =
      &app ? internal::EnvironmentManager::get_instance().get("TZ") : nullptr;
#else
  const char *value = ::getenv("TZ");
#endif
  bool absent = !value;
  const char *key = value ? value : "";
  if (initialized && !force && absent == current_absent &&
      same(key, current_key))
    return current_zone;
  size_t key_size = length(key);
  char *saved = static_cast<char *>(::malloc(key_size + 1));
  if (!saved)
    return cpp::unexpected(ENOMEM);
  struct FreeKey {
    char *p;
    ~FreeKey() { ::free(p); }
  } key_owner{saved};
  for (size_t i = 0; i <= key_size; ++i)
    saved[i] = key[i];
  ZoneSnapshot zone;
  FileData data;
  const char *text = absent ? "" : saved;
  if (*text == ':')
    ++text;
  if (!absent && (!*text || same(text, "UTC") || same(text, "GMT")))
    text = same(text, "GMT") ? "GMT0" : "UTC0";
  auto parsed = parse_posix_timezone(text, zone.rules);
  char *expanded = nullptr;
  struct FreeExpanded {
    char *&p;
    ~FreeExpanded() { ::free(p); }
  } expanded_owner{expanded};
  if (!absent && parsed == TZParseResult::MISSING_RULES) {
    const char suffix[] = ",M3.2.0/2,M11.1.0/2";
    size_t size = length(text);
    if (size > cpp::numeric_limits<size_t>::max() - sizeof(suffix))
      return cpp::unexpected(ENOMEM);
    expanded = static_cast<char *>(::malloc(size + sizeof(suffix)));
    if (!expanded)
      return cpp::unexpected(ENOMEM);
    for (size_t i = 0; i < size; ++i)
      expanded[i] = text[i];
    for (size_t i = 0; i < sizeof(suffix); ++i)
      expanded[size + i] = suffix[i];
    parsed = parse_posix_timezone(expanded, zone.rules);
    if (parsed == TZParseResult::SUCCESS) {
      auto file = data.load("/usr/share/zoneinfo/posixrules");
      if (file) {
        zone.file = *file;
        zone.default_rules = true;
      } else if (resource_error(file.error()))
        return cpp::unexpected(file.error());
    }
  } else if (absent || parsed != TZParseResult::SUCCESS) {
    const char *path = absent ? "/etc/localtime" : text;
    char *allocated = nullptr;
    struct FreePath {
      char *&p;
      ~FreePath() { ::free(p); }
    } path_owner{allocated};
    if (!absent && *path != '/') {
      const char directory[] = "/usr/share/zoneinfo/";
      size_t n = length(path);
      if (n > cpp::numeric_limits<size_t>::max() - sizeof(directory))
        return cpp::unexpected(ENOMEM);
      allocated = static_cast<char *>(::malloc(sizeof(directory) + n));
      if (!allocated)
        return cpp::unexpected(ENOMEM);
      for (size_t i = 0; i < sizeof(directory) - 1; ++i)
        allocated[i] = directory[i];
      for (size_t i = 0; i <= n; ++i)
        allocated[sizeof(directory) - 1 + i] = path[i];
      path = allocated;
    }
    auto file = data.load(path);
    if (file) {
      zone.file = *file;
      zone.geographical = true;
      // For legacy globals, use the footer's recurring standard/DST names when
      // present, otherwise the latest table types. Per-instant tm metadata is
      // always selected from the actual historical type.
      if (file->footer_present())
        zone.rules = file->future_rules();
      else
        for (size_t i = 0; i < file->types_size(); ++i) {
          auto t = file->type(i);
          zone.rules.names[t.daylight] = t.name;
          zone.rules.offsets[t.daylight] = static_cast<int>(t.offset);
          zone.rules.has_dst |= t.daylight;
        }
    } else {
      if (resource_error(file.error()))
        return cpp::unexpected(file.error());
      zone = ZoneSnapshot{};
    }
  }
  for (int i = 0; i < 2; ++i) {
    if (zone.rules.names[i].empty())
      zone.rules.names[i] = i ? "" : "UTC";
    zone.names[i] = intern(zone.rules.names[i]);
    if (!zone.names[i])
      return cpp::unexpected(ENOMEM);
    zone.rules.names[i] = zone.names[i];
  }
#ifdef LIBC_COPT_TZ_VARIABLES
  tzname[0] = const_cast<char *>(zone.names[0]);
  tzname[1] = const_cast<char *>(zone.names[1]);
  timezone = -static_cast<long>(zone.rules.offsets[0]);
  daylight = zone.rules.has_dst;
#endif
  ::free(current_key);
  ::free(current_data);
  current_key = saved;
  key_owner.p = nullptr;
  current_absent = absent;
  current_data = data.bytes;
  data.bytes = nullptr;
  current_zone = zone;
  initialized = true;
  return zone;
}

#else
// Non-POSIX targets retain their UTC-only local-time implementation.
ErrorOr<ZoneSnapshot> snapshot() { return ZoneSnapshot{}; }
#endif

ErrorOr<time_t> shift_seconds(int64_t seconds, int64_t offset) {
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
  ZoneType selected{zone.rules.offsets[is_dst], is_dst, zone.names[is_dst]};
  if (zone.geographical)
    selected = zone.file.at(seconds);
  else if (zone.default_rules)
    selected = zone.file.at_default(seconds, zone.rules);
  int64_t shifted;
  if (__builtin_add_overflow(seconds, selected.offset, &shifted))
    return cpp::unexpected(TIME_OVERFLOW);
  tm local;
  auto status = update_from_seconds(shifted, &local);
  if (!status)
    return cpp::unexpected(status.error());
  set_metadata(&local, zone, selected.daylight);
  local.tm_gmtoff = selected.offset;
  local.tm_zone = intern(selected.name);
  if (!local.tm_zone)
    return cpp::unexpected(ENOMEM);
  *result = local;
  return result;
}

ErrorOr<time_t> invert_geographical(tm *result, const ZoneSnapshot &zone) {
  int64_t wall = civil_seconds(result);
  bool found = false, best_dst = false;
  time_t best = 0;
  // Match against every historical offset, but admit a candidate only when
  // the actual table conversion reproduces the supplied normalized wall time.
  auto consider = [&](int64_t offset) {
    auto candidate = shift_seconds(wall, -offset);
    if (!candidate)
      return;
    tm actual;
    if (!convert(*candidate, &actual, zone) || civil_seconds(&actual) != wall)
      return;
    bool dst = actual.tm_isdst > 0;
    if (result->tm_isdst >= 0 && dst != (result->tm_isdst > 0))
      return;
    bool prefer = !found || (result->tm_isdst < 0 && best_dst && !dst) ||
                  (best_dst == dst && *candidate > best);
    if (prefer) {
      found = true;
      best = *candidate;
      best_dst = dst;
    }
  };
  for (size_t i = 0; i < zone.file.types_size(); ++i)
    consider(zone.file.type(i).offset);
  if (zone.file.footer_present()) {
    consider(zone.file.future_rules().offsets[0]);
    consider(zone.file.future_rules().offsets[1]);
  }
  if (!found) {
    ZoneType before, after;
    if (result->tm_isdst < 0 && zone.file.discontinuity(wall, before, after)) {
      auto a = shift_seconds(wall, -before.offset),
           b = shift_seconds(wall, -after.offset);
      if (!a || !b)
        return cpp::unexpected(TIME_OVERFLOW);
      best = *a > *b ? *a : *b;
    } else {
      auto rules = zone.file.nearby_rules(wall);
      auto standard = shift_seconds(wall, -int64_t(rules.offsets[0]));
      auto summer = shift_seconds(wall, -int64_t(rules.offsets[1]));
      auto selected =
          (result->tm_isdst > 0 && rules.has_dst) ? summer : standard;
      if (result->tm_isdst < 0 && rules.has_dst && standard && summer)
        selected = *standard > *summer ? standard : summer;
      if (!selected)
        return cpp::unexpected(selected.error());
      best = *selected;
    }
  }
  auto status = convert(best, result, zone);
  if (!status)
    return cpp::unexpected(status.error());
  return best;
}

ErrorOr<time_t> invert(tm *result, const ZoneSnapshot &zone) {
  if (zone.geographical)
    return invert_geographical(result, zone);
  int64_t wall = civil_seconds(result);
  auto standard = shift_seconds(wall, -int64_t(zone.rules.offsets[0]));
  auto summer = shift_seconds(wall, -int64_t(zone.rules.offsets[1]));
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
  cpp::lock_guard guard(zone_mutex);
  auto zone = snapshot(true);
  if (!zone)
    return cpp::unexpected(zone.error());
  return {};
}

ErrorOr<cpp::array<const char *, 2>> timezone_names() {
  cpp::lock_guard guard(zone_mutex);
  auto zone = snapshot();
  if (!zone)
    return cpp::unexpected(zone.error());
  return cpp::array<const char *, 2>{zone->names[0], zone->names[1]};
}

ErrorOr<tm *> localtime_internal(const time_t *timer, tm *result) {
  cpp::lock_guard guard(zone_mutex);
  auto zone = snapshot();
  if (!zone)
    return cpp::unexpected(zone.error());
  return convert(*timer, result, *zone);
}

ErrorOr<cpp::optional<GetdateZone>> getdate_timezone_type(size_t index) {
  cpp::lock_guard guard(zone_mutex);
  auto zone = snapshot();
  if (!zone)
    return cpp::unexpected(zone.error());
  ZoneType selected;
  if (zone->geographical && index < zone->file.types_size()) {
    selected = zone->file.type(index);
  } else {
    const PosixTimeZone *rules = &zone->rules;
    if (zone->geographical) {
      index -= zone->file.types_size();
      if (!zone->file.footer_present())
        return cpp::optional<GetdateZone>{};
      rules = &zone->file.future_rules();
    }
    if (index > (rules->has_dst ? 1U : 0U))
      return cpp::optional<GetdateZone>{};
    selected = {rules->offsets[index], index != 0, rules->names[index]};
  }
  const char *name = intern(selected.name);
  if (!name)
    return cpp::unexpected(ENOMEM);
  return cpp::optional<GetdateZone>(
      GetdateZone{selected.offset, selected.daylight, name});
}

ErrorOr<time_t> mktime_local(tm *result) {
  cpp::lock_guard guard(zone_mutex);
  auto zone = snapshot();
  if (!zone)
    return cpp::unexpected(zone.error());
  return invert(result, *zone);
}

ErrorOr<void> prepare_timezone_for_format(tm *result, bool epoch) {
  cpp::lock_guard guard(zone_mutex);
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
