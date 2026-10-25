//===-- Locale character entry point helpers ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/character.h"
#include "hdr/wchar_macros.h"
#include "src/__support/CPP/limits.h"
#ifdef LIBC_FULL_BUILD
#include "src/locale/locale.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace internal {

#ifdef LIBC_FULL_BUILD
const __locale_data *explicit_ctype(locale_t locale) {
  return locale_snapshot(locale).data[LC_CTYPE];
}
#endif

const __locale_data *current_ctype() {
#ifdef LIBC_FULL_BUILD
  return explicit_ctype(thread_locale);
#else
  return nullptr;
#endif
}

namespace {
bool utf8(const __locale_data *category) {
#ifdef LIBC_FULL_BUILD
  return category->utf8;
#else
  (void)category;
  return LIBC_CONF_WCTYPE_MODE == LIBC_WCTYPE_MODE_UTF8;
#endif
}

bool scalar(wint_t c) {
  return c != WEOF && c <= 0x10ffff && (c < 0xd800 || c > 0xdfff) &&
         c == static_cast<wint_t>(static_cast<wchar_t>(c)) &&
         static_cast<wchar_t>(c) >= 0;
}

int ascii_classify(wchar_t c, wctype_t desc) {
  switch (desc) {
  case WCTYPE_ALNUM:
    return ascii::isalnum(c);
  case WCTYPE_ALPHA:
    return ascii::isalpha(c);
  case WCTYPE_BLANK:
    return ascii::isblank(c);
  case WCTYPE_CNTRL:
    return ascii::iscntrl(c);
  case WCTYPE_DIGIT:
    return ascii::isdigit(c);
  case WCTYPE_GRAPH:
    return ascii::isgraph(c);
  case WCTYPE_LOWER:
    return ascii::islower(c);
  case WCTYPE_PRINT:
    return ascii::isprint(c);
  case WCTYPE_PUNCT:
    return ascii::ispunct(c);
  case WCTYPE_SPACE:
    return ascii::isspace(c);
  case WCTYPE_UPPER:
    return ascii::isupper(c);
  case WCTYPE_XDIGIT:
    return ascii::isxdigit(c);
  default:
    return 0;
  }
}

#ifdef LIBC_FULL_BUILD
int regional_classify(wint_t c, wctype_t desc, const CTypeLocaleData &data) {
  using D = CTypeLocaleData;
  switch (desc) {
  case WCTYPE_ALNUM:
    return data.contains(D::ALPHA, c) || data.contains(D::DIGIT, c);
  case WCTYPE_ALPHA:
    return data.contains(D::ALPHA, c);
  case WCTYPE_BLANK:
    return data.contains(D::BLANK, c);
  case WCTYPE_CNTRL:
    return data.contains(D::CNTRL, c);
  case WCTYPE_DIGIT:
    return data.contains(D::DIGIT, c);
  case WCTYPE_GRAPH:
    return data.contains(D::GRAPH, c);
  case WCTYPE_LOWER:
    return data.contains(D::LOWER, c);
  case WCTYPE_PRINT:
    return data.contains(D::PRINT, c);
  case WCTYPE_PUNCT:
    return data.contains(D::PUNCT, c);
  case WCTYPE_SPACE:
    return data.contains(D::SPACE, c);
  case WCTYPE_UPPER:
    return data.contains(D::UPPER, c);
  case WCTYPE_XDIGIT:
    return data.contains(D::XDIGIT, c);
  default:
    return 0;
  }
}
#endif
} // namespace

int classify_wide(wint_t c, wctype_t desc, const __locale_data *category) {
  if (!scalar(c))
    return 0;
#ifdef LIBC_FULL_BUILD
  // Tailoring applies to ASCII too, before the built-in fast path.
  if (category->ctype)
    return regional_classify(c, desc, *category->ctype);
#endif
  if (c < 128 || !utf8(category))
    return ascii_classify(static_cast<wchar_t>(c), desc);
#if defined(LIBC_FULL_BUILD) || LIBC_CONF_WCTYPE_MODE == LIBC_WCTYPE_MODE_UTF8
  // Match the existing C.UTF-8 properties without consulting thread_locale.
  unsigned properties = lookup_properties(static_cast<wchar_t>(c));
  switch (desc) {
  case WCTYPE_ALNUM:
  case WCTYPE_ALPHA:
    return properties & PropertyFlag::ALPHA;
  case WCTYPE_BLANK:
    return properties & PropertyFlag::BLANK;
  case WCTYPE_CNTRL:
    return properties & PropertyFlag::CNTRL;
  case WCTYPE_GRAPH:
    return (properties & (PropertyFlag::PRINT | PropertyFlag::SPACE)) ==
           PropertyFlag::PRINT;
  case WCTYPE_LOWER:
    return properties & PropertyFlag::LOWER;
  case WCTYPE_PRINT:
    return properties & PropertyFlag::PRINT;
  case WCTYPE_PUNCT:
    return properties & PropertyFlag::PUNCT;
  case WCTYPE_SPACE:
    return properties & PropertyFlag::SPACE;
  case WCTYPE_UPPER:
    return properties & PropertyFlag::UPPER;
  default:
    // C.UTF-8 digits and hexadecimal digits are ASCII-only.
    return 0;
  }
#else
  return 0;
#endif
}

wint_t map_wide(wint_t c, bool upper, const __locale_data *category) {
  if (!scalar(c))
    return c;
#ifdef LIBC_FULL_BUILD
  if (category->ctype)
    return category->ctype->map(
        upper ? CTypeLocaleData::TOUPPER : CTypeLocaleData::TOLOWER, c);
#endif
  wchar_t wc = static_cast<wchar_t>(c);
  if (c < 128 || !utf8(category))
    return upper ? ascii::toupper(wc) : ascii::tolower(wc);
#if defined(LIBC_FULL_BUILD) || LIBC_CONF_WCTYPE_MODE == LIBC_WCTYPE_MODE_UTF8
  return upper ? wctype_internal::toupper(wc) : wctype_internal::tolower(wc);
#else
  return c;
#endif
}

int classify_byte(int c, wctype_t desc, const __locale_data *category) {
  if (c < 0 || c > cpp::numeric_limits<unsigned char>::max())
    return 0;
#ifdef LIBC_FULL_BUILD
  if (category->ctype) {
    // Only ASCII bytes encode complete characters in UTF-8. The other
    // currently supported category encoding maps bytes to like-valued scalars.
    if (category->utf8 && c >= 128)
      return 0;
    return regional_classify(c, desc, *category->ctype);
  }
#else
  (void)category;
#endif
  // C/POSIX's full-byte conversion does not imply Latin-1 classification.
  return ascii_classify(static_cast<wchar_t>(c), desc);
}

int map_byte(int c, bool upper, const __locale_data *category) {
  if (c < 0 || c > cpp::numeric_limits<unsigned char>::max())
    return c;
#ifdef LIBC_FULL_BUILD
  if (category->ctype) {
    unsigned limit = category->utf8 ? 127 : 255;
    if (static_cast<unsigned>(c) > limit)
      return c;
    auto mapped = category->ctype->map(
        upper ? CTypeLocaleData::TOUPPER : CTypeLocaleData::TOLOWER, c);
    return mapped <= limit ? static_cast<int>(mapped) : c;
  }
#else
  (void)category;
#endif
  return upper ? ascii::toupper(static_cast<wchar_t>(c))
               : ascii::tolower(static_cast<wchar_t>(c));
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
