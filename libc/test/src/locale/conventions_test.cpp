//===-- Locale convention consumers and ownership tests -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/langinfo_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/libc_errno.h"
#include "src/langinfo/nl_langinfo.h"
#include "src/langinfo/nl_langinfo_l.h"
#include "src/locale/conventions.h"
#include "src/locale/localeconv.h"
#include "src/locale/uselocale.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;

namespace {
struct Fixture {
  uint8_t bytes[2048]{};
  size_t size = 130;
  size_t section;
  unsigned entry;
  static void put(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(n >> (24 - i * 8));
  }
  void item(unsigned key, unsigned kind, const void *data, size_t length) {
    auto *slot = bytes + section + 16 * entry++;
    put(slot, key);
    put(slot + 4, kind);
    put(slot + 8, uint32_t(size - section));
    put(slot + 12, uint32_t(length));
    for (size_t i = 0; i != length; ++i)
      bytes[size++] = static_cast<const uint8_t *>(data)[i];
  }
  void text(unsigned key, const char *str) {
    size_t length = 1;
    while (str[length - 1])
      ++length;
    item(key, 1, str, length);
  }
  explicit Fixture(bool unspecified = false) {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = uint8_t(magic[i]);
    put(bytes + 8, 1);
    put(bytes + 12, 0x01020304);
    put(bytes + 20, 6);
    put(bytes + 24, 128);
    put(bytes + 28, 2);
    bytes[128] = 'x';
    for (unsigned category = 0; category != 6; ++category) {
      auto *directory = bytes + 32 + 16 * category;
      section = size;
      entry = 0;
      const unsigned count = category == 1 ? 3 : category == 4 ? 21 : 0;
      size += 16 * count;
      if (category == 1) {
        text(1, ",");
        text(2, "\xe2\x80\xaf");
        const uint8_t grouping[] = {3, 2, 127, 0};
        item(3, 3, grouping, sizeof(grouping));
      } else if (category == 4) {
        const char *strings[] = {"TRY ", "\xe2\x82\xba", ".", ",", "+", "-"};
        for (unsigned i = 0; i != 6; ++i)
          text(i + 1, unspecified ? "" : strings[i]);
        const uint8_t grouping[] = {3, 0};
        item(7, 3, grouping, sizeof(grouping));
        const uint32_t numbers[] = {2, 1, 1, 2, 0, 1, 3, 4, 0, 1, 1, 2, 4, 3};
        for (unsigned i = 0; i != 14; ++i) {
          uint8_t number[4];
          put(number, unspecified ? 127 : numbers[i]);
          item(i + 8, 2, number, sizeof(number));
        }
      }
      put(directory, category);
      put(directory + 4, uint32_t(section));
      put(directory + 8, uint32_t(size - section));
      put(directory + 12, count);
    }
    put(bytes + 16, uint32_t(size));
  }
};

struct Selection {
  __locale_t locale = locale_from_mask(0);
  locale_t previous;
  Selection() : previous(LIBC_NAMESPACE::uselocale(&locale)) {}
  ~Selection() {
    LIBC_NAMESPACE::uselocale(previous);
    release_locale(locale);
  }
};
} // namespace

TEST(LlvmLibcConventionsTest, MixedCategoriesAndExplicitQueries) {
  Fixture fixture;
  auto file = LocaleDataFile::copy({fixture.bytes, fixture.size});
  ASSERT_TRUE(file.has_value());
  auto numeric = create_numeric_category(**file);
  auto monetary = create_monetary_category(**file);
  (*file)->release();
  ASSERT_TRUE(numeric.has_value());
  ASSERT_TRUE(monetary.has_value());
  Selection selected;
  selected.locale.__data[LC_NUMERIC] = *numeric;
  selected.locale.__data[LC_MONETARY] = *monetary;
  LIBC_NAMESPACE::libc_errno = EDOM;
  auto *value = LIBC_NAMESPACE::localeconv();
  EXPECT_STREQ(value->decimal_point, ",");
  EXPECT_STREQ(value->thousands_sep, "\xe2\x80\xaf");
  EXPECT_EQ(value->grouping[0], char(3));
  EXPECT_EQ(value->grouping[1], char(2));
  EXPECT_EQ(value->grouping[2], char(CHAR_MAX));
  EXPECT_EQ(value->grouping[3], char(0));
  EXPECT_STREQ(value->int_curr_symbol, "TRY ");
  EXPECT_STREQ(value->currency_symbol, "\xe2\x82\xba");
  EXPECT_STREQ(value->mon_decimal_point, ".");
  EXPECT_STREQ(value->mon_thousands_sep, ",");
  EXPECT_STREQ(value->mon_grouping, "\3");
  EXPECT_STREQ(value->positive_sign, "+");
  EXPECT_STREQ(value->negative_sign, "-");
  const char fields[] = {value->int_frac_digits,   value->frac_digits,
                         value->p_cs_precedes,     value->p_sep_by_space,
                         value->n_cs_precedes,     value->n_sep_by_space,
                         value->p_sign_posn,       value->n_sign_posn,
                         value->int_p_cs_precedes, value->int_p_sep_by_space,
                         value->int_n_cs_precedes, value->int_n_sep_by_space,
                         value->int_p_sign_posn,   value->int_n_sign_posn};
  const char expected[] = {2, 1, 1, 2, 0, 1, 3, 4, 0, 1, 1, 2, 4, 3};
  for (unsigned i = 0; i != sizeof(fields); ++i)
    EXPECT_EQ(fields[i], expected[i]);
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo(RADIXCHAR), ",");
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo(THOUSEP), "\xe2\x80\xaf");
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo(CRNCYSTR), "-\xe2\x82\xba");
  auto c = locale_from_mask(0);
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(RADIXCHAR, &c), ".");
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(CRNCYSTR, &c), "");
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(-1, &selected.locale), "");
  EXPECT_EQ(static_cast<int>(LIBC_NAMESPACE::libc_errno), EDOM);
  LIBC_NAMESPACE::uselocale(&c);
  EXPECT_STREQ(LIBC_NAMESPACE::localeconv()->decimal_point, ".");
  EXPECT_STREQ(LIBC_NAMESPACE::localeconv()->currency_symbol, "");
  EXPECT_EQ(LIBC_NAMESPACE::localeconv()->frac_digits, char(CHAR_MAX));
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo_l(RADIXCHAR, &selected.locale), ",");
  LIBC_NAMESPACE::uselocale(&selected.locale);
}

TEST(LlvmLibcConventionsTest, UnspecifiedMonetaryValues) {
  Fixture fixture(true);
  auto file = LocaleDataFile::copy({fixture.bytes, fixture.size});
  ASSERT_TRUE(file.has_value());
  auto category = create_monetary_category(**file);
  (*file)->release();
  ASSERT_TRUE(category.has_value());
  Selection selected;
  selected.locale.__data[LC_MONETARY] = *category;
  auto *value = LIBC_NAMESPACE::localeconv();
  EXPECT_STREQ(value->currency_symbol, "");
  EXPECT_STREQ(LIBC_NAMESPACE::nl_langinfo(CRNCYSTR), "");
  const char fields[] = {value->int_frac_digits,   value->frac_digits,
                         value->p_cs_precedes,     value->p_sep_by_space,
                         value->n_cs_precedes,     value->n_sep_by_space,
                         value->p_sign_posn,       value->n_sign_posn,
                         value->int_p_cs_precedes, value->int_p_sep_by_space,
                         value->int_n_cs_precedes, value->int_n_sep_by_space,
                         value->int_p_sign_posn,   value->int_n_sign_posn};
  for (char field : fields)
    EXPECT_EQ(field, char(CHAR_MAX));
}

#ifdef CONVENTIONS_ALLOCATION_TEST
namespace {
unsigned fail_at = 0, calls = 0, live = 0;
bool tracking = false;
void *allocations[8]{};
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t n) {
  if (tracking && ++calls == fail_at)
    return nullptr;
  void *p = __real_malloc(n);
  if (tracking && p)
    for (void *&slot : allocations)
      if (!slot) {
        slot = p;
        ++live;
        break;
      }
  return p;
}
extern "C" void __wrap_free(void *p) {
  if (p)
    for (void *&slot : allocations)
      if (slot == p) {
        slot = nullptr;
        --live;
        break;
      }
  __real_free(p);
}

TEST(LlvmLibcConventionsTest, AllocationRollbackAndLastRelease) {
  Fixture fixture;
  auto file = LocaleDataFile::copy({fixture.bytes, fixture.size});
  ASSERT_TRUE(file.has_value());
  for (unsigned kind = 0; kind != 2; ++kind) {
    unsigned count = kind ? 3 : 2;
    for (unsigned failure = 1; failure <= count; ++failure) {
      fail_at = failure;
      calls = 0;
      tracking = true;
      auto category = kind ? create_monetary_category(**file)
                           : create_numeric_category(**file);
      tracking = false;
      EXPECT_FALSE(category.has_value());
      EXPECT_EQ(category.error(), ENOMEM);
      EXPECT_EQ(live, 0U);
    }
    calls = fail_at = 0;
    tracking = true;
    auto category = kind ? create_monetary_category(**file)
                         : create_numeric_category(**file);
    tracking = false;
    ASSERT_TRUE(category.has_value());
    EXPECT_EQ(live, count);
    EXPECT_TRUE(retain_locale_category(*category));
    release_locale_category(*category);
    EXPECT_EQ(live, count);
    release_locale_category(*category);
    EXPECT_EQ(live, 0U);
  }
  (*file)->release();
}
#endif
