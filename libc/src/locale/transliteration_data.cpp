//===-- Validated transliteration records ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/transliteration_data.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
bool scalar(uint32_t value) {
  return value != 0 && value <= 0x10ffff && (value < 0xd800 || value > 0xdfff);
}

bool before(const TransliterationData::Rule &a,
            const TransliterationData::Rule &b) {
  size_t n =
      a.source_size() < b.source_size() ? a.source_size() : b.source_size();
  for (size_t i = 0; i != n; ++i) {
    if (a.source(i) != b.source(i))
      return a.source(i) < b.source(i);
  }
  return a.source_size() < b.source_size();
}
} // namespace

ErrorOr<LocaleDataView::Item>
TransliterationData::Rule::alternative(size_t index) const {
  if (index >= alternatives())
    return Error(EINVAL);
  size_t offset = 2 + source_size();
  for (size_t i = 0; i != index; ++i)
    offset += 1 + words.word(offset).value();
  size_t length = words.word(offset).value();
  return LocaleDataView::Item{
      LocaleDataView::Kind::WORDS,
      words.bytes.subspan((offset + 1) * 4, length * 4)};
}

ErrorOr<TransliterationData>
TransliterationData::read(const LocaleDataView &file) {
  auto size = file.find(0, 0x300);
  if (!size || size->kind != LocaleDataView::Kind::WORDS ||
      size->bytes.size() != 4)
    return Error(EINVAL);
  size_t count = size->word(0).value();
  if (count > MAX_RULES)
    return Error(EINVAL);
  TransliterationData result(file, count);
  size_t total = 0;
  for (size_t i = 0; i != count; ++i) {
    auto entry = file.find(0, 0x10000 + uint32_t(i));
    if (!entry || entry->kind != LocaleDataView::Kind::WORDS)
      return Error(EINVAL);
    size_t words = entry->bytes.size() / 4;
    if (words < 4 || words > MAX_WORDS - total)
      return Error(EINVAL);
    total += words;
    size_t n = entry->word(0).value();
    if (!n || n > MAX_SEQUENCE || n > words - 3)
      return Error(EINVAL);
    for (size_t j = 1; j <= n; ++j)
      if (!scalar(entry->word(j).value()))
        return Error(EINVAL);
    size_t alternatives = entry->word(n + 1).value();
    if (!alternatives || alternatives > MAX_ALTERNATIVES)
      return Error(EINVAL);
    size_t offset = n + 2;
    for (size_t j = 0; j != alternatives; ++j) {
      if (offset == words)
        return Error(EINVAL);
      size_t length = entry->word(offset++).value();
      if (length > MAX_SEQUENCE || length > words - offset)
        return Error(EINVAL);
      for (size_t k = 0; k != length; ++k)
        if (!scalar(entry->word(offset++).value()))
          return Error(EINVAL);
    }
    if (offset != words)
      return Error(EINVAL);
    if (i && !before(result.rule(i - 1).value(), Rule{*entry}))
      return Error(EINVAL);
  }
  return result;
}

ErrorOr<TransliterationData::Rule>
TransliterationData::rule(size_t index) const {
  if (index >= count)
    return Error(EINVAL);
  return Rule{file.find(0, 0x10000 + uint32_t(index)).value()};
}

ErrorOr<TransliterationData::Rule>
TransliterationData::find(cpp::span<const uint32_t> source) const {
  auto candidate = prefix(source);
  if (!candidate || candidate->source_size() == source.size())
    return candidate;
  return Error(ENOENT);
}

ErrorOr<TransliterationData::Rule>
TransliterationData::prefix(cpp::span<const uint32_t> source) const {
  if (source.empty() || source.size() > MAX_SEQUENCE)
    return Error(EINVAL);
  for (uint32_t value : source)
    if (!scalar(value))
      return Error(EINVAL);
  size_t low = 0, high = count;
  while (low != high) {
    size_t mid = low + (high - low) / 2;
    Rule candidate = rule(mid).value();
    size_t i = 0;
    while (i < source.size() && i < candidate.source_size() &&
           source[i] == candidate.source(i))
      ++i;
    bool less = i == source.size() || (i < candidate.source_size() &&
                                       source[i] < candidate.source(i));
    if (less)
      high = mid;
    else
      low = mid + 1;
  }
  if (low == count)
    return Error(ENOENT);
  auto candidate = rule(low).value();
  if (candidate.source_size() < source.size())
    return Error(ENOENT);
  for (size_t i = 0; i != source.size(); ++i)
    if (candidate.source(i) != source[i])
      return Error(ENOENT);
  return candidate;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
