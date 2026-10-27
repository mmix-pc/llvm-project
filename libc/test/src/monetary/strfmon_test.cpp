//===-- Monetary formatting tests ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/fenv_macros.h"
#include "src/__support/libc_errno.h"
#include "src/fenv/fegetround.h"
#include "src/fenv/fesetround.h"
#include "src/locale/locale.h"
#include "src/locale/uselocale.h"
#include "src/monetary/strfmon.h"
#include "src/monetary/strfmon_l.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;
namespace {
using M = MonetaryLocaleData;
struct Selection {
  __locale_data category{"money-test", true};
  __locale_t locale = locale_from_mask(0);
  locale_t previous;
  const uint8_t groups[2] = {3, 0};
  Selection() {
    M data{};
    const char *strings[] = {"USD ", "$", ".", ",", "", "-"};
    for (unsigned i = 0; i != M::TEXT_COUNT; ++i) {
      size_t length = 0;
      while (strings[i][length])
        ++length;
      data.strings[i] = {
          LocaleDataView::Kind::STRING,
          {reinterpret_cast<const uint8_t *>(strings[i]), length + 1}};
    }
    data.grouping = {LocaleDataView::Kind::BYTES, groups};
    const uint8_t values[] = {2, 2, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1};
    for (unsigned i = 0; i != M::NUMBER_COUNT; ++i)
      data.numbers[i] = values[i];
    category.monetary = LIBC_NAMESPACE::cpp::optional<M>(data);
    locale.data[LC_MONETARY] = &category;
    previous = LIBC_NAMESPACE::uselocale(&locale);
  }
  ~Selection() { LIBC_NAMESPACE::uselocale(previous); }
  void text(M::Text index, const char *value) {
    size_t size = 0;
    while (value[size])
      ++size;
    category.monetary->strings[index].bytes = {
        reinterpret_cast<const uint8_t *>(value), size + 1};
  }
  void number(M::Number index, unsigned value) {
    category.monetary->numbers[index] = value;
  }
};
} // namespace

TEST(LlvmLibcStrfmonTest, CurrencyAndLiteral) {
  Selection selection;
  char out[128];
  EXPECT_EQ(
      LIBC_NAMESPACE::strfmon(out, sizeof(out), "[%n][%i]%%", 1234.5, -1234.5),
      ssize_t(27));
  EXPECT_STREQ(out, "[$1,234.50][-USD 1,234.50]%");
  EXPECT_EQ(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%^!n", 1234.5),
            ssize_t(7));
  EXPECT_STREQ(out, "1234.50");
}

TEST(LlvmLibcStrfmonTest, StandardAlignmentExamples) {
  Selection selection;
  const char *formats[] = {"%n",     "%11n",     "%#5n",    "%=*#5n",
                           "%=0#5n", "%^#5.0n",  "%^#5.4n", "%(#5n",
                           "%!(#5n", "%-14#5.4n"};
  const char *positive[] = {"$123.45",       "    $123.45",  " $   123.45",
                            " $***123.45",   " $000123.45",  " $  123",
                            " $  123.4500",  " $   123.45 ", "    123.45 ",
                            " $   123.4500 "};
  const char *negative[] = {"-$123.45",      "   -$123.45",  "-$   123.45",
                            "-$***123.45",   "-$000123.45",  "-$  123",
                            "-$  123.4500",  "($   123.45)", "(   123.45)",
                            "-$   123.4500 "};
  char out[128];
  for (unsigned i = 0; i != sizeof(formats) / sizeof(*formats); ++i) {
    EXPECT_GE(LIBC_NAMESPACE::strfmon(out, sizeof(out), formats[i], 123.45),
              ssize_t(0));
    EXPECT_STREQ(out, positive[i]);
    EXPECT_GE(LIBC_NAMESPACE::strfmon(out, sizeof(out), formats[i], -123.45),
              ssize_t(0));
    EXPECT_STREQ(out, negative[i]);
  }
}

TEST(LlvmLibcStrfmonTest, AllSignPositions) {
  Selection selection;
  char out[64];
  const char *before[] = {"($12.00)", "-$12.00", "$12.00-", "-$12.00",
                          "$-12.00"};
  const char *after[] = {"(12.00$)", "-12.00$", "12.00$-", "12.00-$",
                         "12.00$-"};
  for (unsigned i = 0; i != 5; ++i) {
    selection.number(M::N_SIGN_POSN, i);
    selection.number(M::N_CS_PRECEDES, 1);
    EXPECT_GE(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%n", -12.0),
              ssize_t(0));
    EXPECT_STREQ(out, before[i]);
    selection.number(M::N_CS_PRECEDES, 0);
    EXPECT_GE(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%n", -12.0),
              ssize_t(0));
    EXPECT_STREQ(out, after[i]);
  }
}

TEST(LlvmLibcStrfmonTest, LocaleIndependenceAndMultibyte) {
  Selection selection;
  selection.text(M::DECIMAL_POINT, ",");
  selection.text(M::THOUSANDS_SEP, "\xe2\x80\xaf");
  selection.text(M::CURRENCY_SYMBOL, "\xe2\x82\xac");
  char out[64];
  auto c = locale_from_mask(0);
  LIBC_NAMESPACE::uselocale(&c);
  EXPECT_EQ(LIBC_NAMESPACE::strfmon_l(out, sizeof(out), &selection.locale, "%n",
                                      1234.5),
            ssize_t(13));
  EXPECT_STREQ(out, "\xe2\x82\xac"
                    "1\xe2\x80\xaf"
                    "234,50");
  EXPECT_TRUE(LIBC_NAMESPACE::uselocale(nullptr) == &c);
  EXPECT_EQ(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%n", -1234.5),
            ssize_t(8));
  EXPECT_STREQ(out, "-1234.50");
}

TEST(LlvmLibcStrfmonTest, SpacingModesAndInternationalSeparator) {
  Selection selection;
  char out[64];
  const char *before[] = {"($ 12.00)", "-$ 12.00", "$ 12.00-", "-$ 12.00",
                          "$- 12.00",  "($12.00)", "- $12.00", "$12.00 -",
                          "- $12.00",  "$ -12.00"};
  const char *after[] = {"(12.00 $)", "-12.00 $", "12.00 $-", "12.00 -$",
                         "12.00 $-",  "(12.00$)", "- 12.00$", "12.00$ -",
                         "12.00- $",  "12.00$ -"};
  for (unsigned space = 1; space != 3; ++space) {
    selection.number(M::N_SEP_BY_SPACE, space);
    for (unsigned sign = 0; sign != 5; ++sign) {
      selection.number(M::N_SIGN_POSN, sign);
      selection.number(M::N_CS_PRECEDES, 1);
      EXPECT_GE(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%n", -12.0),
                ssize_t(0));
      EXPECT_STREQ(out, before[(space - 1) * 5 + sign]);
      selection.number(M::N_CS_PRECEDES, 0);
      EXPECT_GE(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%n", -12.0),
                ssize_t(0));
      EXPECT_STREQ(out, after[(space - 1) * 5 + sign]);
    }
  }
  selection.text(M::INT_CURR_SYMBOL, "USD_");
  EXPECT_GE(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%i", -12.0), ssize_t(0));
  EXPECT_STREQ(out, "-USD_12.00");
  EXPECT_GE(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%#1n", 12.0),
            ssize_t(0));
  EXPECT_STREQ(out, "$12.00");
}

TEST(LlvmLibcStrfmonTest, LargeFieldsAndPrecision) {
  Selection selection;
  char out[1024];
  EXPECT_EQ(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%.300n", 1.0),
            ssize_t(303));
  EXPECT_EQ(out[0], '$');
  EXPECT_EQ(out[1], '1');
  EXPECT_EQ(out[2], '.');
  for (unsigned i = 3; i != 303; ++i)
    EXPECT_EQ(out[i], '0');
  EXPECT_EQ(out[303], '\0');
  EXPECT_EQ(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%#999999999n", 1.0),
            ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), E2BIG);
  EXPECT_EQ(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%.2147483648n", 1.0),
            ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), E2BIG);
}

TEST(LlvmLibcStrfmonTest, IndependentDomesticAndInternationalFields) {
  Selection selection;
  selection.text(M::POSITIVE_SIGN, "+");
  selection.number(M::P_SIGN_POSN, 1);
  selection.number(M::N_SIGN_POSN, 0);
  selection.number(M::INT_P_SIGN_POSN, 2);
  selection.number(M::INT_N_SIGN_POSN, 4);
  selection.number(M::INT_P_CS_PRECEDES, 0);
  selection.number(M::INT_N_CS_PRECEDES, 1);
  selection.number(M::INT_P_SEP_BY_SPACE, 1);
  selection.number(M::INT_N_SEP_BY_SPACE, 0);
  selection.number(M::INT_FRAC_DIGITS, 3);
  char out[128];
  EXPECT_GE(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%n|%i", 12.5, 12.5),
            ssize_t(0));
  EXPECT_STREQ(out, "+$12.50|12.500 USD+");
  EXPECT_GE(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%n|%i", -12.5, -12.5),
            ssize_t(0));
  EXPECT_STREQ(out, "($12.50)|USD-12.500");
  EXPECT_GE(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%n|%n", 0.0, -0.0),
            ssize_t(0));
  EXPECT_STREQ(out, "+$0.00|($0.00)");
}

#ifdef MONETARY_ALLOCATION_TEST
namespace {
bool tracking = false, fail_allocation = false;
void *allocation = nullptr;
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t bytes) {
  if (tracking && fail_allocation)
    return nullptr;
  void *result = __real_malloc(bytes);
  if (tracking)
    allocation = result;
  return result;
}
extern "C" void __wrap_free(void *p) {
  if (p == allocation)
    allocation = nullptr;
  __real_free(p);
}
TEST(LlvmLibcStrfmonTest, AllocationFailureAndErrorCleanup) {
  Selection selection;
  char out[1024];
  fail_allocation = tracking = true;
  auto result = LIBC_NAMESPACE::strfmon(out, sizeof(out), "%.300n", 1.0);
  tracking = false;
  EXPECT_EQ(result, ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
  EXPECT_EQ(allocation, nullptr);
  fail_allocation = false;
  tracking = true;
  result = LIBC_NAMESPACE::strfmon(out, 301, "%.300n", 1.0);
  tracking = false;
  EXPECT_EQ(result, ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), E2BIG);
  EXPECT_EQ(allocation, nullptr);
}
#endif

TEST(LlvmLibcStrfmonTest, CapacityAndInvalidFormat) {
  Selection selection;
  char out[16];
  for (size_t capacity = 0; capacity != 7; ++capacity) {
    for (char &c : out)
      c = '@';
    LIBC_NAMESPACE::libc_errno = 0;
    EXPECT_EQ(LIBC_NAMESPACE::strfmon(out, capacity, "%n", 12.0), ssize_t(-1));
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), E2BIG);
    for (size_t i = capacity; i != sizeof(out); ++i)
      EXPECT_EQ(out[i], '@');
  }
  EXPECT_EQ(LIBC_NAMESPACE::strfmon(out, 7, "%n", 12.0), ssize_t(6));
  EXPECT_STREQ(out, "$12.00");
  const char *invalid[] = {"%", "%=", "%+((n", "%#n", "%.n", "%q"};
  for (const char *format : invalid) {
    EXPECT_EQ(LIBC_NAMESPACE::strfmon(out, sizeof(out), format, 1.0),
              ssize_t(-1));
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
  }
  EXPECT_EQ(LIBC_NAMESPACE::strfmon(out, sizeof(out),
                                    "%999999999999999999999999n", 1.0),
            ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), E2BIG);
}

TEST(LlvmLibcStrfmonTest, SignFallbackAndRounding) {
  Selection selection;
  char out[64];
  auto c = locale_from_mask(0);
  EXPECT_EQ(LIBC_NAMESPACE::strfmon_l(out, sizeof(out), &c, "%+n", 1.0),
            ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
  EXPECT_EQ(LIBC_NAMESPACE::strfmon_l(out, sizeof(out), &c, "%n", -0.0),
            ssize_t(5));
  EXPECT_STREQ(out, "-0.00");
  int previous = LIBC_NAMESPACE::fegetround();
  LIBC_NAMESPACE::fesetround(FE_DOWNWARD);
  EXPECT_EQ(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%.1n", -1.25),
            ssize_t(5));
  EXPECT_STREQ(out, "-$1.3");
  LIBC_NAMESPACE::fesetround(FE_UPWARD);
  EXPECT_EQ(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%.1n", -1.25),
            ssize_t(5));
  EXPECT_STREQ(out, "-$1.2");
  LIBC_NAMESPACE::fesetround(previous);
  EXPECT_EQ(LIBC_NAMESPACE::strfmon(out, sizeof(out), "%.1n", 999.96875),
            ssize_t(8));
  EXPECT_STREQ(out, "$1,000.0");
}
