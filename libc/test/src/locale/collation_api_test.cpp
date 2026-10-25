//===-- Locale collation API tests
//-----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/wchar_macros.h"
#include "src/__support/libc_errno.h"
#include "src/locale/locale_storage.h"
#include "src/locale/uselocale.h"
#include "src/string/strcoll.h"
#include "src/string/strcoll_l.h"
#include "src/string/strxfrm.h"
#include "src/string/strxfrm_l.h"
#include "src/wchar/wcscoll.h"
#include "src/wchar/wcscoll_l.h"
#include "src/wchar/wcsxfrm.h"
#include "src/wchar/wcsxfrm_l.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;
using LIBC_NAMESPACE::libc_errno;

#ifdef COLLATION_ALLOCATION_TEST
namespace {
int remaining = -1;
void *allocations[8]{};
unsigned live = 0;
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (remaining == 0)
    return nullptr;
  void *p = __real_malloc(size);
  if (remaining > 0) {
    --remaining;
    if (p)
      allocations[live++] = p;
  }
  return p;
}
extern "C" void __wrap_free(void *p) {
  for (unsigned i = 0; i != live; ++i)
    if (allocations[i] == p) {
      allocations[i] = allocations[--live];
      break;
    }
  __real_free(p);
}
#endif

namespace {
struct Fixture {
  uint8_t bytes[330]{};
  __locale_t object = locale_from_mask(0);
  __locale_data *category = nullptr;
  static void word(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(value >> (24 - 8 * i));
  }
  bool create() {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = uint8_t(magic[i]);
    word(bytes + 8, 1);
    word(bytes + 12, 0x01020304);
    word(bytes + 20, 6);
    word(bytes + 24, 128);
    word(bytes + 28, 2);
    bytes[128] = 'x';
    // Explicit a and z reverse code-point order. Other characters use the
    // validated implicit UNDEFINED rule.
    const uint32_t records[][14] = {{2, 3, 1, 1, 1, 1, 1},
                                    {1, 'a', 1, 1, 1, 1, 1, 2, 0, 0, 0},
                                    {1, 'z', 1, 1, 1, 1, 1, 1, 0, 0, 0}};
    const size_t counts[] = {7, 11, 11};
    size_t end = 130 + 48;
    for (unsigned i = 0; i != 3; ++i) {
      auto *entry = bytes + 130 + 16 * i;
      word(entry, i ? 0x1000 + i - 1 : 1);
      word(entry + 4, 2);
      word(entry + 8, uint32_t(end - 130));
      word(entry + 12, uint32_t(counts[i] * 4));
      for (size_t j = 0; j != counts[i]; ++j)
        word(bytes + end + 4 * j, records[i][j]);
      end += counts[i] * 4;
    }
    word(bytes + 16, uint32_t(end));
    for (unsigned i = 0; i != 6; ++i) {
      auto *entry = bytes + 32 + i * 16;
      word(entry, i);
      word(entry + 4, uint32_t(i <= 3 ? 130 : end));
      word(entry + 8, uint32_t(i == 3 ? end - 130 : 0));
      word(entry + 12, i == 3 ? 3 : 0);
    }
    auto file = LocaleDataFile::copy({bytes, end});
    if (!file)
      return false;
    auto record = create_collation_category(**file, true);
    (*file)->release();
    if (!record)
      return false;
    category = *record;
    object.data[LC_COLLATE] = category;
    return true;
  }
  ~Fixture() {
    if (category)
      release_locale_category(category);
  }
};
unsigned int key_value(char c) { return static_cast<unsigned char>(c); }
wchar_t key_value(wchar_t c) { return c; }
template <class Char> int sign(const Char *a, const Char *b) {
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return (key_value(*a) > key_value(*b)) - (key_value(*a) < key_value(*b));
}
} // namespace

TEST(LlvmLibcCollationAPITest, ExplicitAndCurrentCategories) {
  Fixture f;
  ASSERT_TRUE(f.create());
  libc_errno = EDOM;
  EXPECT_GT(LIBC_NAMESPACE::strcoll_l("a", "z", &f.object), 0);
  EXPECT_GT(LIBC_NAMESPACE::wcscoll_l(L"a", L"z", &f.object), 0);
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_LT(LIBC_NAMESPACE::strcoll("a", "z"), 0);
  auto previous = LIBC_NAMESPACE::uselocale(&f.object);
  EXPECT_GT(LIBC_NAMESPACE::strcoll("a", "z"), 0);
  EXPECT_GT(LIBC_NAMESPACE::wcscoll(L"a", L"z"), 0);
  LIBC_NAMESPACE::uselocale(previous);
  EXPECT_LT(LIBC_NAMESPACE::wcscoll(L"a", L"z"), 0);
  wchar_t low[] = {WCHAR_MIN, 0}, high[] = {WCHAR_MAX, 0};
  EXPECT_LT(LIBC_NAMESPACE::wcscoll(low, high), 0);
  EXPECT_GT(LIBC_NAMESPACE::wcscoll(high, low), 0);
}

TEST(LlvmLibcCollationAPITest, ComparisonAndTransformAgree) {
  Fixture f;
  ASSERT_TRUE(f.create());
  const char *strings[] = {"", "a", "z", "az", "za", "\xc3\xa4", "b"};
  const wchar_t *wide[] = {L"", L"a", L"z", L"az", L"za", L"\u00e4", L"b"};
  char keys[7][128];
  wchar_t wide_keys[7][128];
  auto previous = LIBC_NAMESPACE::uselocale(&f.object);
  for (size_t i = 0; i != 7; ++i) {
    libc_errno = EDOM;
    size_t n = LIBC_NAMESPACE::strxfrm_l(keys[i], strings[i], 128, &f.object);
    EXPECT_LT(n, size_t(128));
    EXPECT_EQ(LIBC_NAMESPACE::strxfrm(nullptr, strings[i], 0), n);
    EXPECT_EQ(LIBC_NAMESPACE::wcsxfrm_l(wide_keys[i], wide[i], 128, &f.object),
              n);
    EXPECT_EQ(LIBC_NAMESPACE::wcsxfrm(nullptr, wide[i], 0), n);
    EXPECT_EQ(int(libc_errno), EDOM);
    for (size_t j = 0; j <= n; ++j)
      EXPECT_EQ(wchar_t(static_cast<unsigned char>(keys[i][j])),
                wide_keys[i][j]);
    char guarded[130];
    for (size_t cap = 0; cap <= n + 1; ++cap) {
      for (auto &c : guarded)
        c = 42;
      EXPECT_EQ(LIBC_NAMESPACE::strxfrm(guarded + 1, strings[i], cap), n);
      EXPECT_EQ(guarded[0], char(42));
      EXPECT_EQ(guarded[cap + 1], char(42));
    }
  }
  for (size_t i = 0; i != 7; ++i)
    for (size_t j = 0; j != 7; ++j) {
      EXPECT_EQ(LIBC_NAMESPACE::strcoll(strings[i], strings[j]),
                sign(keys[i], keys[j]));
      EXPECT_EQ(LIBC_NAMESPACE::wcscoll(wide[i], wide[j]),
                sign(wide_keys[i], wide_keys[j]));
    }
  LIBC_NAMESPACE::uselocale(previous);
}

TEST(LlvmLibcCollationAPITest, InvalidEncodingDoesNotPublishKey) {
  Fixture f;
  ASSERT_TRUE(f.create());
  char dest[16] = "unchanged";
  const char *bad[] = {"\xc0\x80", "\xed\xa0\x80", "\xe2\x82", "\xff"};
  for (const char *text : bad) {
    libc_errno = 0;
    EXPECT_EQ(LIBC_NAMESPACE::strxfrm_l(dest, text, 16, &f.object), size_t(-1));
    EXPECT_EQ(int(libc_errno), EILSEQ);
    EXPECT_STREQ(dest, "unchanged");
    libc_errno = 0;
    EXPECT_EQ(LIBC_NAMESPACE::strcoll_l(text, "a", &f.object), 0);
    EXPECT_EQ(int(libc_errno), EILSEQ);
  }
  const wchar_t bad_wide[] = {0xd800, 0};
  wchar_t output[2] = {42, 42};
  EXPECT_EQ(LIBC_NAMESPACE::wcsxfrm_l(output, bad_wide, 2, &f.object),
            size_t(-1));
  EXPECT_EQ(output[0], wchar_t(42));
}

#ifdef COLLATION_ALLOCATION_TEST
TEST(LlvmLibcCollationAPITest, AllocationRollback) {
  Fixture f;
  ASSERT_TRUE(f.create());
  for (int fail = 0; fail != 4; ++fail) {
    remaining = fail;
    libc_errno = 0;
    int result = LIBC_NAMESPACE::strcoll_l("a", "z", &f.object);
    remaining = -1;
    EXPECT_EQ(result, 0);
    EXPECT_EQ(int(libc_errno), ENOMEM);
    EXPECT_EQ(live, 0U);
  }
  for (int fail = 0; fail != 2; ++fail) {
    char output[16] = "unchanged";
    remaining = fail;
    size_t result = LIBC_NAMESPACE::strxfrm_l(output, "a", 16, &f.object);
    remaining = -1;
    EXPECT_EQ(result, size_t(-1));
    EXPECT_EQ(int(libc_errno), ENOMEM);
    EXPECT_STREQ(output, "unchanged");
    EXPECT_EQ(live, 0U);
  }
}
#endif
