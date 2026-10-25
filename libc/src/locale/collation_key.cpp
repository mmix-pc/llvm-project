//===-- Collation comparison and transform keys
//----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/collation_key.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/optional.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {

// Both comparisons and serialized keys consume this stream. Zero separates
// levels; one terminates a position-sensitive element; other tokens are
// distances or weights plus two. Backward sections reverse elements, never
// the expansion inside an element.
class KeyTokens {
  cpp::span<const CollationData::Rule> elements;
  unsigned level = 0;
  size_t next_element = 0;
  size_t backward_begin = 0, backward_cursor = 0;
  CollationData::Weights weights{{LocaleDataView::Kind::WORDS, {}}};
  size_t weight_index = 0;
  bool position_end = false;

  cpp::optional<CollationData::Rule> next_rule() {
    if (backward_cursor > backward_begin)
      return elements[--backward_cursor];
    if (next_element == elements.size())
      return cpp::nullopt;
    if ((elements[next_element].direction(level).value() & 2) == 0)
      return elements[next_element++];
    backward_begin = next_element;
    do {
      ++next_element;
    } while (next_element != elements.size() &&
             (elements[next_element].direction(level).value() & 2));
    backward_cursor = next_element;
    return elements[--backward_cursor];
  }

public:
  explicit KeyTokens(cpp::span<const CollationData::Rule> elements)
      : elements(elements) {}

  cpp::optional<uint64_t> next() {
    if (level == 4)
      return cpp::nullopt;
    if (weight_index != weights.size())
      return uint64_t(weights.at(weight_index++).value()) + 2;
    if (position_end) {
      position_end = false;
      return uint64_t(1);
    }
    size_t distance = 0;
    while (auto rule = next_rule()) {
      ++distance;
      weights = rule->weights(level).value();
      weight_index = 0;
      if (weights.size()) {
        if (rule->direction(level).value() & 4) {
          position_end = true;
          return uint64_t(distance) + 2;
        }
        return uint64_t(weights.at(weight_index++).value()) + 2;
      }
    }
    ++level;
    next_element = backward_begin = backward_cursor = 0;
    weights = {{LocaleDataView::Kind::WORDS, {}}};
    weight_index = 0;
    return uint64_t(0);
  }
};

} // namespace

ErrorOr<size_t> resolve_collation(const CollationData &data,
                                  cpp::span<const uint32_t> source,
                                  cpp::span<CollationData::Rule> elements) {
  size_t count = 0;
  while (!source.empty()) {
    if (count == elements.size())
      return Error(E2BIG);
    auto rule = data.lookup(source);
    if (!rule)
      return Error(EILSEQ);
    elements[count++] = *rule;
    source = source.subspan(rule->source_size());
  }
  return count;
}

int compare_collation(cpp::span<const CollationData::Rule> left,
                      cpp::span<const CollationData::Rule> right) {
  KeyTokens a(left), b(right);
  while (true) {
    auto x = a.next(), y = b.next();
    if (!x || !y)
      return x.has_value() - y.has_value();
    if (*x != *y)
      return *x < *y ? -1 : 1;
  }
}

template <class Char>
static ErrorOr<size_t>
transform_key(cpp::span<const CollationData::Rule> elements,
              cpp::span<Char> destination) {
  // A real element array cannot approach SIZE_MAX entries. Keep the token
  // distance arithmetic explicit even for deliberately constructed spans.
  if (elements.size() > size_t(-1) / sizeof(CollationData::Rule))
    return Error(EOVERFLOW);
  KeyTokens tokens(elements);
  size_t length = 0;
  while (auto token = tokens.next()) {
    uint8_t buffer[10];
    size_t n = 0;
    uint64_t value = *token;
    if (value < 2) {
      buffer[n++] = uint8_t(value + 1);
    } else {
      // Length-first base-253 digits preserve integer order and reserve bytes
      // 1/2 for the two separators. No byte is a string terminator.
      uint8_t digits[9];
      size_t count = 0;
      do {
        digits[count++] = uint8_t(value % 253 + 3);
        value /= 253;
      } while (value);
      buffer[n++] = uint8_t(count + 3);
      while (count)
        buffer[n++] = digits[--count];
    }
    if (n > size_t(-1) - length)
      return Error(EOVERFLOW);
    for (size_t i = 0; i != n; ++i)
      if (length + i < destination.size())
        destination[length + i] = buffer[i];
    length += n;
  }
  if (length < destination.size())
    destination[length] = 0;
  return length;
}

ErrorOr<size_t>
transform_collation(cpp::span<const CollationData::Rule> elements,
                    cpp::span<uint8_t> destination) {
  return transform_key(elements, destination);
}

ErrorOr<size_t>
transform_collation(cpp::span<const CollationData::Rule> elements,
                    cpp::span<wchar_t> destination) {
  return transform_key(elements, destination);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
