//===-- Checked MMIX TZif decoding ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/time/linux/mmix/tzif.h"
#include "src/__support/CPP/limits.h"
namespace LIBC_NAMESPACE_DECL {
namespace time_utils {
namespace {
uint32_t u32(const unsigned char *p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 |
         p[3];
}
int64_t signed_time(const unsigned char *p, unsigned width) {
  uint64_t value = 0;
  for (unsigned i = 0; i < width; ++i)
    value = (value << 8) | p[i];
  if (!(p[0] & 128))
    return static_cast<int64_t>(value);
  uint64_t mask = width == 4 ? 0xffffffffULL : ~uint64_t(0);
  return -1 - static_cast<int64_t>((~value) & mask);
}
struct Header {
  unsigned version;
  uint32_t ut, standard, leaps, transitions, types, chars;
};
bool header(const unsigned char *p, size_t size, Header &h) {
  if (size < 44 || p[0] != 'T' || p[1] != 'Z' || p[2] != 'i' || p[3] != 'f')
    return false;
  if (p[4] != 0 && p[4] != '2' && p[4] != '3' && p[4] != '4')
    return false;
  h = {p[4],        u32(p + 20), u32(p + 24), u32(p + 28),
       u32(p + 32), u32(p + 36), u32(p + 40)};
  return h.types && h.types <= 256 && h.chars && (!h.ut || h.ut == h.types) &&
         (!h.standard || h.standard == h.types);
}
bool block_size(const Header &h, unsigned width, size_t available,
                size_t &size) {
  // Counts are 32-bit; do arithmetic in 64 bits before any size_t conversion.
  uint64_t total = uint64_t(h.transitions) * (width + 1) +
                   uint64_t(h.types) * 6 + h.chars +
                   uint64_t(h.leaps) * (width + 4) + h.standard + h.ut;
  if (total > available)
    return false;
  size = static_cast<size_t>(total);
  return true;
}
} // namespace
TZifView::Result TZifView::parse(const unsigned char *data, size_t size,
                                 TZifView &view) {
  Header h;
  if (!header(data, size, h))
    return Result::INVALID;
  if (h.leaps)
    return Result::LEAP_RECORDS;
  size_t bytes;
  if (!block_size(h, 4, size - 44, bytes))
    return Result::INVALID;
  unsigned width = 4;
  const unsigned char *block = data + 44;
  size_t remaining = size - 44;
  if (h.version) {
    unsigned version = h.version;
    block += bytes;
    remaining -= bytes;
    if (!header(block, remaining, h) || h.version != version)
      return Result::INVALID;
    if (h.leaps)
      return Result::LEAP_RECORDS;
    block += 44;
    remaining -= 44;
    width = 8;
    if (!block_size(h, width, remaining, bytes))
      return Result::INVALID;
  }
  TZifView next;
  next.width = width;
  next.count = h.transitions;
  next.type_count = h.types;
  next.name_size = h.chars;
  next.transitions = block;
  next.indices = block + size_t(h.transitions) * width;
  next.types = next.indices + h.transitions;
  next.names = reinterpret_cast<const char *>(next.types + size_t(h.types) * 6);
  for (size_t i = 0; i < h.transitions; ++i) {
    if (next.indices[i] >= h.types ||
        (i && signed_time(block + i * width, width) <=
                  signed_time(block + (i - 1) * width, width)))
      return Result::INVALID;
  }
  for (size_t i = 0; i < h.types; ++i) {
    const unsigned char *t = next.types + i * 6;
    if (t[4] > 1 || t[5] >= h.chars)
      return Result::INVALID;
    size_t end = t[5];
    while (end < h.chars && next.names[end])
      ++end;
    if (end == h.chars)
      return Result::INVALID;
  }
  const unsigned char *standard =
      reinterpret_cast<const unsigned char *>(next.names) + h.chars;
  const unsigned char *ut = standard + h.standard;
  next.standard_flags = standard;
  next.ut_flags = ut;
  next.has_standard_flags = h.standard != 0;
  next.has_ut_flags = h.ut != 0;
  for (size_t i = 0; i < h.standard; ++i)
    if (standard[i] > 1)
      return Result::INVALID;
  for (size_t i = 0; i < h.ut; ++i)
    if (ut[i] > 1 || (ut[i] && (!h.standard || !standard[i])))
      return Result::INVALID;
  const unsigned char *tail = block + bytes;
  remaining -= bytes;
  if (width == 4) {
    if (remaining)
      return Result::INVALID;
  } else {
    if (remaining < 2 || tail[0] != '\n' || tail[remaining - 1] != '\n')
      return Result::INVALID;
    if (remaining > 2) {
      cpp::string_view rule(reinterpret_cast<const char *>(tail + 1),
                            remaining - 2);
      if (parse_posix_timezone(rule, next.footer) != TZParseResult::SUCCESS)
        return Result::INVALID;
      next.has_footer = true;
    }
  }
  view = next;
  return Result::SUCCESS;
}
ZoneType TZifView::type(size_t index) const {
  const unsigned char *t = types + index * 6;
  const char *name = names + t[5];
  size_t length = 0;
  while (name[length])
    ++length;
  return {signed_time(t, 4), t[4] != 0, cpp::string_view(name, length)};
}
ZoneType TZifView::at(int64_t seconds) const {
  if (has_footer &&
      (!count ||
       seconds > signed_time(transitions + (count - 1) * width, width))) {
    int64_t days = seconds / 86400 - (seconds % 86400 < 0);
    int64_t year = civil_date(days).year;
    // Bound recurring-rule arithmetic; public conversion checks tm_year too.
    if (year >= int64_t(cpp::numeric_limits<int>::min()) + 1899 &&
        year <= int64_t(cpp::numeric_limits<int>::max()) + 1901) {
      bool dst = footer.is_dst(seconds, year);
      return {footer.offsets[dst], dst, footer.names[dst]};
    }
  }
  size_t low = 0, high = count;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (signed_time(transitions + mid * width, width) <= seconds)
      low = mid + 1;
    else
      high = mid;
  }
  return type(low ? indices[low - 1] : 0);
}
ZoneType TZifView::at_default(int64_t seconds,
                              const PosixTimeZone &requested) const {
  bool dst = false;
  int64_t last = 0;
  for (size_t i = 0; i < count; ++i) {
    size_t next_index = indices[i];
    ZoneType previous = type(i ? indices[i - 1] : 0);
    int64_t transition = signed_time(transitions + i * width, width);
    bool utc = has_ut_flags && ut_flags[next_index];
    bool standard = has_standard_flags && standard_flags[next_index];
    if (!utc) {
      int64_t old_offset = previous.offset;
      bool old_dst = previous.daylight;
      if (standard) {
        old_dst = false;
        for (size_t j = i + 1; j; --j) {
          ZoneType nearby = type(j > 1 ? indices[j - 2] : 0);
          if (!nearby.daylight) {
            old_offset = nearby.offset;
            break;
          }
        }
      }
      int64_t delta = old_offset - requested.offsets[old_dst];
      if (__builtin_add_overflow(transition, delta, &transition))
        continue; // Outside representable timestamps; cannot match this query.
    }
    last = transition;
    if (seconds < transition)
      break;
    dst = type(next_index).daylight;
  }
  if (has_footer && (!count || seconds > last)) {
    PosixTimeZone rules = footer;
    rules.offsets[0] = requested.offsets[0];
    rules.offsets[1] = requested.offsets[1];
    int64_t year = civil_date(seconds / 86400 - (seconds % 86400 < 0)).year;
    if (year >= int64_t(cpp::numeric_limits<int>::min()) + 1899 &&
        year <= int64_t(cpp::numeric_limits<int>::max()) + 1901)
      dst = rules.is_dst(seconds, year);
  }
  return {requested.offsets[dst], dst, requested.names[dst]};
}
bool TZifView::discontinuity(int64_t wall, ZoneType &before,
                             ZoneType &after) const {
  for (size_t i = 0; i < count; ++i) {
    ZoneType a = type(i ? indices[i - 1] : 0), b = type(indices[i]);
    int64_t t = signed_time(transitions + i * width, width), left, right;
    if (__builtin_add_overflow(t, a.offset, &left) ||
        __builtin_add_overflow(t, b.offset, &right))
      continue;
    if ((wall >= left && wall < right) || (wall >= right && wall < left)) {
      before = a;
      after = b;
      return true;
    }
  }
  return false;
}
PosixTimeZone TZifView::nearby_rules(int64_t seconds) const {
  if (has_footer &&
      (!count ||
       seconds > signed_time(transitions + (count - 1) * width, width)))
    return footer;
  PosixTimeZone result;
  bool found[2] = {false, false};
  size_t cursor = 0;
  while (cursor < count &&
         signed_time(transitions + cursor * width, width) <= seconds)
    ++cursor;
  for (size_t j = cursor + 1; j; --j) {
    auto t = type(j > 1 ? indices[j - 2] : 0);
    if (!found[t.daylight]) {
      result.offsets[t.daylight] = static_cast<int>(t.offset);
      result.names[t.daylight] = t.name;
      found[t.daylight] = true;
    }
    if (found[0] && found[1])
      break;
  }
  for (size_t j = cursor; j < count && !(found[0] && found[1]); ++j) {
    auto t = type(indices[j]);
    if (!found[t.daylight]) {
      result.offsets[t.daylight] = static_cast<int>(t.offset);
      result.names[t.daylight] = t.name;
      found[t.daylight] = true;
    }
  }
  result.has_dst = found[1];
  if (!found[0] && found[1]) {
    result.offsets[0] = result.offsets[1];
    result.names[0] = result.names[1];
  }
  return result;
}
} // namespace time_utils
} // namespace LIBC_NAMESPACE_DECL
