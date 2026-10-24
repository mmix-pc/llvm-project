//===-- Validated character category data ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/ctype_data.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
using Table = LocaleDataView::Item;

bool scalar(uint32_t value) {
  return value <= 0x10ffff && (value < 0xd800 || value > 0xdfff);
}

uint32_t word(const Table &table, size_t index) {
  return table.word(index).value();
}

bool valid_table(const Table &table, bool ranges) {
  if (table.kind != LocaleDataView::Kind::WORDS || table.bytes.size() % 8)
    return false;
  uint32_t previous = 0;
  for (size_t i = 0; i < table.bytes.size() / 4; i += 2) {
    uint32_t first = word(table, i), second = word(table, i + 1);
    if (!scalar(first) || !scalar(second) || (i && first <= previous))
      return false;
    if (ranges && (second < first || (first <= 0xdfff && second >= 0xd800)))
      return false;
    previous = ranges ? second : first;
  }
  return true;
}

bool subset(const Table &small, const Table &big) {
  size_t cursor = 0;
  for (size_t i = 0; i < small.bytes.size() / 4; i += 2) {
    uint32_t first = word(small, i), last = word(small, i + 1);
    while (true) {
      while (cursor < big.bytes.size() / 4 && word(big, cursor + 1) < first)
        cursor += 2;
      if (cursor == big.bytes.size() / 4 || word(big, cursor) > first)
        return false;
      uint32_t end = word(big, cursor + 1);
      if (end >= last)
        break;
      first = end + 1;
      cursor += 2;
    }
  }
  return true;
}

bool disjoint(const Table &left, const Table &right) {
  size_t a = 0, b = 0;
  while (a < left.bytes.size() / 4 && b < right.bytes.size() / 4) {
    if (word(left, a + 1) < word(right, b))
      a += 2;
    else if (word(right, b + 1) < word(left, a))
      b += 2;
    else
      return false;
  }
  return true;
}
} // namespace

ErrorOr<CTypeLocaleData> CTypeLocaleData::read(const LocaleDataView &file) {
  CTypeLocaleData result{};
  for (unsigned i = 0; i != CLASS_COUNT; ++i) {
    auto table = file.find(0, i + 1);
    if (!table || !valid_table(*table, true))
      return Error(EINVAL);
    result.classes[i] = *table;
  }
  for (unsigned i = 0; i != MAPPING_COUNT; ++i) {
    auto table = file.find(0, 0x101 + i);
    if (!table || !valid_table(*table, false))
      return Error(EINVAL);
    result.mappings[i] = *table;
  }
  const auto &c = result.classes;
  if (!subset(c[UPPER], c[ALPHA]) || !subset(c[LOWER], c[ALPHA]) ||
      !subset(c[ALPHA], c[GRAPH]) || !subset(c[DIGIT], c[GRAPH]) ||
      !subset(c[PUNCT], c[GRAPH]) || !subset(c[GRAPH], c[PRINT]) ||
      !subset(c[DIGIT], c[XDIGIT]) || !subset(c[BLANK], c[SPACE]) ||
      !disjoint(c[ALPHA], c[DIGIT]) || !disjoint(c[GRAPH], c[SPACE]) ||
      !disjoint(c[PRINT], c[CNTRL]) || !disjoint(c[PUNCT], c[ALPHA]) ||
      !disjoint(c[PUNCT], c[DIGIT]))
    return Error(EINVAL);
  return result;
}

bool CTypeLocaleData::contains(Class kind, uint32_t value) const {
  if (unsigned(kind) >= CLASS_COUNT || !scalar(value))
    return false;
  const auto &table = classes[kind];
  size_t low = 0, high = table.bytes.size() / 8;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (value < word(table, mid * 2))
      high = mid;
    else if (value > word(table, mid * 2 + 1))
      low = mid + 1;
    else
      return true;
  }
  return false;
}

uint32_t CTypeLocaleData::map(Mapping kind, uint32_t value) const {
  if (unsigned(kind) >= MAPPING_COUNT || !scalar(value))
    return value;
  const auto &table = mappings[kind];
  size_t low = 0, high = table.bytes.size() / 8;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    uint32_t key = word(table, mid * 2);
    if (value < key)
      high = mid;
    else if (value > key)
      low = mid + 1;
    else
      return word(table, mid * 2 + 1);
  }
  return value;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
