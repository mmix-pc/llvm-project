//===-- Validated collation records ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/collation_data.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
bool scalar(uint32_t value) {
  return value <= 0x10ffff && (value < 0xd800 || value > 0xdfff);
}

bool before(const CollationData::Rule &a, const CollationData::Rule &b) {
  size_t n =
      a.source_size() < b.source_size() ? a.source_size() : b.source_size();
  for (size_t i = 0; i != n; ++i)
    if (a.source(i) != b.source(i))
      return a.source(i) < b.source(i);
  return a.source_size() < b.source_size();
}
} // namespace

ErrorOr<uint32_t> CollationData::Weights::at(size_t index) const {
  if (index >= size())
    return Error(EINVAL);
  return single ? ErrorOr<uint32_t>(single) : words.word(index);
}

ErrorOr<uint32_t> CollationData::Rule::direction(size_t level) const {
  if (level >= 4)
    return Error(EINVAL);
  return fallback_base
             ? ErrorOr<uint32_t>((fallback_directions >> (level * 4)) & 7)
             : words.word(1 + source_size() + level);
}

ErrorOr<CollationData::Weights>
CollationData::Rule::weights(size_t level) const {
  if (level >= 4)
    return Error(EINVAL);
  if (fallback_base)
    return Weights{{LocaleDataView::Kind::WORDS, {}},
                   fallback_base + (level ? 1 + fallback_scalar : 0)};
  size_t offset = 5 + source_size();
  for (size_t i = 0; i != level; ++i)
    offset += 1 + words.word(offset).value();
  size_t length = words.word(offset).value();
  return Weights{{LocaleDataView::Kind::WORDS,
                  words.bytes.subspan(4 * (offset + 1), 4 * length)}};
}

ErrorOr<CollationData> CollationData::read(const LocaleDataView &file) {
  auto header = file.find(3, 1);
  if (!header || header->kind != LocaleDataView::Kind::WORDS ||
      header->bytes.size() != 28 || header->word(0).value() > MAX_RULES ||
      header->word(1).value() > MAX_WEIGHT || header->word(2).value() != 1)
    return Error(EINVAL);
  uint32_t fallback_directions = 0;
  for (unsigned level = 0; level != 4; ++level) {
    uint32_t direction = header->word(3 + level).value();
    if (direction != 1 && direction != 2 && direction != 5 && direction != 6)
      return Error(EINVAL);
    fallback_directions |= direction << (level * 4);
  }
  CollationData result(file, header->word(0).value(),
                       header->word(1).value() + 1, fallback_directions);
  uint32_t max_weight = header->word(1).value();
  size_t total = 0;
  for (size_t i = 0; i != result.count; ++i) {
    auto entry = file.find(3, 0x1000 + uint32_t(i));
    if (!entry || entry->kind != LocaleDataView::Kind::WORDS)
      return Error(EINVAL);
    size_t size = entry->bytes.size() / 4;
    if (size < 10 || size > MAX_WORDS - total)
      return Error(EINVAL);
    total += size;
    size_t n = entry->word(0).value();
    if (!n || n > MAX_SEQUENCE || n > size - 9)
      return Error(EINVAL);
    if (n > result.longest_source)
      result.longest_source = n;
    for (size_t j = 1; j <= n; ++j)
      if (!scalar(entry->word(j).value()))
        return Error(EINVAL);
    for (size_t j = n + 1; j != n + 5; ++j) {
      auto direction = entry->word(j).value();
      if (direction != 1 && direction != 2 && direction != 5 && direction != 6)
        return Error(EINVAL);
      if ((direction & 4) != (header->word(3 + j - n - 1).value() & 4))
        return Error(EINVAL);
    }
    size_t offset = n + 5;
    for (size_t level = 0; level != 4; ++level) {
      if (offset == size)
        return Error(EINVAL);
      size_t length = entry->word(offset++).value();
      if (length > MAX_SEQUENCE || length > size - offset)
        return Error(EINVAL);
      for (size_t j = 0; j != length; ++j) {
        auto weight = entry->word(offset++).value();
        if (!weight || weight > max_weight)
          return Error(EINVAL);
      }
    }
    if (offset != size ||
        (i && !before(result.rule(i - 1).value(), Rule{*entry})))
      return Error(EINVAL);
  }
  return result;
}

ErrorOr<CollationData::Rule> CollationData::rule(size_t index) const {
  if (index >= count)
    return Error(EINVAL);
  return Rule{file.find(3, 0x1000 + uint32_t(index)).value()};
}

ErrorOr<CollationData::Rule>
CollationData::find(cpp::span<const uint32_t> source) const {
  if (source.empty() || source.size() > MAX_SEQUENCE)
    return Error(EINVAL);
  for (uint32_t value : source)
    if (!scalar(value))
      return Error(EINVAL);
  size_t low = 0, high = count;
  while (low != high) {
    size_t mid = low + (high - low) / 2;
    auto candidate = rule(mid).value();
    size_t i = 0;
    while (i != source.size() && i != candidate.source_size() &&
           source[i] == candidate.source(i))
      ++i;
    if (i == source.size() && i == candidate.source_size())
      return candidate;
    if (i == candidate.source_size() ||
        (i != source.size() && candidate.source(i) < source[i]))
      low = mid + 1;
    else
      high = mid;
  }
  return Error(ENOENT);
}

ErrorOr<CollationData::Rule>
CollationData::longest(cpp::span<const uint32_t> source) const {
  size_t n = source.size() < longest_source ? source.size() : longest_source;
  if (!n)
    return Error(EINVAL);
  for (size_t i = 0; i != n; ++i)
    if (!scalar(source[i]))
      return Error(EINVAL);
  while (n) {
    auto candidate = find(source.first(n--));
    if (candidate)
      return candidate;
  }
  return Error(ENOENT);
}

ErrorOr<CollationData::Rule>
CollationData::lookup(cpp::span<const uint32_t> source) const {
  auto explicit_rule = longest(source);
  if (explicit_rule || explicit_rule.error() != ENOENT)
    return explicit_rule;
  return Rule{{LocaleDataView::Kind::WORDS, {}},
              source[0],
              fallback_base,
              fallback_directions};
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
