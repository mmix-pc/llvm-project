//===-- Locale collation entry point helpers ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/collation.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/libc_errno.h"
#include "src/__support/wchar/scalar_codec.h"
#include "src/locale/collation_key.h"
#ifdef LIBC_FULL_BUILD
#include "src/locale/locale.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace internal {

const __locale_data *explicit_collation(locale_t locale) {
#ifdef LIBC_FULL_BUILD
  return locale_snapshot(locale).data[LC_COLLATE];
#else
  (void)locale;
  return nullptr;
#endif
}

const __locale_data *current_collation() {
#ifdef LIBC_FULL_BUILD
  return explicit_collation(thread_locale);
#else
  return nullptr;
#endif
}

namespace {
template <class Char> size_t length(const Char *source) {
  size_t size = 0;
  while (source[size])
    ++size;
  return size;
}

unsigned int ordered(char value) { return static_cast<unsigned char>(value); }
wchar_t ordered(wchar_t value) { return value; }

template <class Char> int builtin_compare(const Char *a, const Char *b) {
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  // Relational comparison also handles the full signed wchar_t domain.
  return (ordered(*a) > ordered(*b)) - (ordered(*a) < ordered(*b));
}

ErrorOr<DecodedScalar> decode(const char *source, size_t size, bool utf8) {
  return decode_scalar(utf8 ? ScalarEncoding::UTF8 : ScalarEncoding::LATIN1,
                       {reinterpret_cast<const uint8_t *>(source), size});
}

ErrorOr<DecodedScalar> decode(const wchar_t *source, size_t, bool) {
  uint32_t scalar = uint32_t(*source);
  if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
    return Error(EILSEQ);
  return DecodedScalar{scalar, 1};
}

class Elements {
  uint32_t *scalars = nullptr;
  CollationData::Rule *rules = nullptr;
  size_t count = 0;

public:
  Elements() = default;
  Elements(const Elements &) = delete;
  Elements &operator=(const Elements &) = delete;
  ~Elements() {
    int saved_errno = libc_errno;
    ::free(rules);
    ::free(scalars);
    libc_errno = saved_errno;
  }
  cpp::span<const CollationData::Rule> view() const { return {rules, count}; }

  template <class Char>
  int prepare(const CollationData &data, const Char *source, bool utf8) {
    size_t size = length(source);
    if (!size)
      return 0;
    if (size > size_t(-1) / sizeof(uint32_t) ||
        size > size_t(-1) / sizeof(CollationData::Rule))
      return EOVERFLOW;
    AllocChecker ac;
    scalars = new (ac) uint32_t[size];
    if (!ac)
      return ENOMEM;
    size_t used = 0;
    while (size) {
      auto scalar = decode(source, size, utf8);
      if (!scalar)
        return EILSEQ;
      scalars[used++] = scalar->value;
      source += scalar->bytes;
      size -= scalar->bytes;
    }
    rules = new (ac) CollationData::Rule[used];
    if (!ac)
      return ENOMEM;
    auto result = resolve_collation(data, {scalars, used}, {rules, used});
    if (!result)
      return result.error();
    count = *result;
    return 0;
  }
};

bool is_utf8(const __locale_data *category) {
#ifdef LIBC_FULL_BUILD
  return category->utf8;
#else
  (void)category;
  return false;
#endif
}

const CollationData *validated(const __locale_data *category) {
#ifdef LIBC_FULL_BUILD
  return category && category->collation ? &*category->collation : nullptr;
#else
  (void)category;
  return nullptr;
#endif
}

template <class Char>
int compare(const Char *left, const Char *right,
            const __locale_data *category) {
  // A named category may explicitly inherit C ordering while retaining its
  // data file for ownership/name queries. Storage alone does not select rules.
  const auto *data = validated(category);
  if (!data)
    return builtin_compare(left, right);
  int saved_errno = libc_errno;
  Elements a, b;
  int error = a.prepare(*data, left, is_utf8(category));
  if (!error)
    error = b.prepare(*data, right, is_utf8(category));
  if (error) {
    libc_errno = error;
    return 0;
  }
  int result = compare_collation(a.view(), b.view());
  libc_errno = saved_errno;
  return result;
}

ErrorOr<size_t> key(cpp::span<const CollationData::Rule> elements, char *dest,
                    size_t capacity) {
  return transform_collation(elements,
                             {reinterpret_cast<uint8_t *>(dest), capacity});
}
ErrorOr<size_t> key(cpp::span<const CollationData::Rule> elements,
                    wchar_t *dest, size_t capacity) {
  return transform_collation(elements, {dest, capacity});
}

size_t builtin_transform(char *dest, const char *source, size_t capacity) {
  size_t size = length(source);
  if (size < capacity)
    for (size_t i = 0; i <= size; ++i)
      dest[i] = source[i];
  return size;
}

size_t builtin_transform(wchar_t *dest, const wchar_t *source,
                         size_t capacity) {
  size_t size = length(source);
  if (capacity) {
    size_t written = size < capacity ? size : capacity - 1;
    for (size_t i = 0; i < written; ++i)
      dest[i] = source[i];
    dest[written] = 0;
  }
  return size;
}

template <class Char>
size_t transform(Char *dest, const Char *source, size_t capacity,
                 const __locale_data *category) {
  const auto *data = validated(category);
  if (!data)
    return builtin_transform(dest, source, capacity);
  int saved_errno = libc_errno;
  Elements elements;
  int error = elements.prepare(*data, source, is_utf8(category));
  if (error) {
    libc_errno = error;
    return size_t(-1);
  }
  auto result = key(elements.view(), dest, capacity);
  if (!result) {
    libc_errno = result.error();
    return size_t(-1);
  }
  libc_errno = saved_errno;
  return *result;
}
} // namespace

int collate(const char *left, const char *right,
            const __locale_data *category) {
  return compare(left, right, category);
}
int collate(const wchar_t *left, const wchar_t *right,
            const __locale_data *category) {
  return compare(left, right, category);
}
size_t collate_transform(char *dest, const char *source, size_t capacity,
                         const __locale_data *category) {
  return transform(dest, source, capacity, category);
}
size_t collate_transform(wchar_t *dest, const wchar_t *source, size_t capacity,
                         const __locale_data *category) {
  return transform(dest, source, capacity, category);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
