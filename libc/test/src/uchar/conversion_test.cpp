//===-- Uchar conversion tests--------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "hdr/errno_macros.h"
#include "src/__support/libc_errno.h"
#include "src/uchar/c16rtomb.h"
#include "src/uchar/c32rtomb.h"
#include "src/uchar/mbrtoc16.h"
#include "src/uchar/mbrtoc32.h"
#include "src/wchar/mbsinit.h"
#include "test/UnitTest/Test.h"

namespace ll = LIBC_NAMESPACE;

TEST(LlvmLibcUcharTest, ScalarRoundTripAndFragmentation) {
  const char32_t values[] = {0,      0x7f,   0x80,   0x7ff,   0x800,
                             0xd7ff, 0xe000, 0xffff, 0x10000, 0x10ffff};
  for (char32_t value : values) {
    mbstate_t encode = {};
    char bytes[4] = {};
    size_t count = ll::c32rtomb(bytes, value, &encode);
    ASSERT_TRUE(count >= 1 && count <= 4);
    EXPECT_NE(ll::mbsinit(&encode), 0);
    for (size_t split = 0; split < count; ++split) {
      mbstate_t decode = {};
      char32_t out = 123;
      EXPECT_EQ(ll::mbrtoc32(&out, bytes, split, &decode), size_t(-2));
      EXPECT_EQ(unsigned(out), 123u);
      EXPECT_EQ(ll::mbrtoc32(&out, bytes + split, count - split, &decode),
                value ? count - split : size_t(0));
      EXPECT_EQ(unsigned(out), unsigned(value));
      EXPECT_NE(ll::mbsinit(&decode), 0);
    }
    if (value <= 0xffff) {
      char narrow[4] = {};
      EXPECT_EQ(ll::c16rtomb(narrow, static_cast<char16_t>(value), &encode),
                count);
      for (size_t i = 0; i < count; ++i)
        EXPECT_EQ(narrow[i], bytes[i]);
      char16_t out = 123;
      EXPECT_EQ(ll::mbrtoc16(&out, bytes, count, &encode),
                value ? count : size_t(0));
      EXPECT_EQ(unsigned(out), unsigned(value));
    }
  }
}

TEST(LlvmLibcUcharTest, Surrogates) {
  const char32_t scalars[] = {0x10000, 0x1f600, 0x10ffff};
  for (char32_t scalar : scalars) {
    char16_t high = static_cast<char16_t>(0xd800 + ((scalar - 0x10000) >> 10));
    char16_t low = 0xdc00 + ((scalar - 0x10000) & 0x3ff);
    mbstate_t state = {};
    char bytes[5] = {'x', 'x', 'x', 'x', 'x'};
    EXPECT_EQ(ll::c16rtomb(bytes, high, &state), size_t(0));
    EXPECT_EQ(bytes[0], 'x');
    EXPECT_EQ(ll::mbsinit(&state), 0);
    EXPECT_EQ(ll::c16rtomb(bytes, low, &state), size_t(4));
    EXPECT_EQ(bytes[4], 'x');
    EXPECT_NE(ll::mbsinit(&state), 0);
    char16_t out = 0;
    EXPECT_EQ(ll::mbrtoc16(&out, bytes, 2, &state), size_t(-2));
    EXPECT_EQ(ll::mbrtoc16(&out, bytes + 2, 2, &state), size_t(2));
    EXPECT_EQ(unsigned(out), unsigned(high));
    EXPECT_EQ(ll::mbsinit(&state), 0);
    EXPECT_EQ(ll::mbrtoc16(&out, "A", 0, &state), size_t(-3));
    EXPECT_EQ(unsigned(out), unsigned(low));
    EXPECT_NE(ll::mbsinit(&state), 0);
    EXPECT_EQ(ll::mbrtoc16(&out, "A", 1, &state), size_t(1));
    EXPECT_EQ(unsigned(out), unsigned('A'));
  }
}

TEST(LlvmLibcUcharTest, InvalidSequences) {
  const char *invalid[] = {
      "\x80",         "\xc0\x80",         "\xed\xa0\x80", "\xf4\x90\x80\x80",
      "\xe0\x80\x80", "\xf0\x80\x80\x80", "\xff"};
  for (const char *s : invalid) {
    mbstate_t state = {};
    char32_t out = 123;
    libc_errno = 0;
    EXPECT_EQ(ll::mbrtoc32(&out, s, 4, &state), size_t(-1));
    EXPECT_EQ(int(libc_errno), EILSEQ);
    EXPECT_EQ(unsigned(out), 123u);
  }
  const char32_t invalid_scalars[] = {0xd800, 0xdfff, 0x110000};
  for (char32_t c : invalid_scalars) {
    mbstate_t state = {};
    char out[4] = {'x'};
    libc_errno = 0;
    EXPECT_EQ(ll::c32rtomb(out, c, &state), size_t(-1));
    EXPECT_EQ(int(libc_errno), EILSEQ);
    EXPECT_EQ(out[0], 'x');
  }
  mbstate_t state = {};
  char out[4];
  EXPECT_EQ(ll::c16rtomb(out, 0xdc00, &state), size_t(-1));
  EXPECT_EQ(ll::c16rtomb(out, 0xd800, &state), size_t(0));
  EXPECT_EQ(ll::c16rtomb(out, 'A', &state), size_t(-1));
  EXPECT_EQ(int(libc_errno), EILSEQ);
}

TEST(LlvmLibcUcharTest, NullArgumentsAndImplicitStates) {
  mbstate_t state = {};
  char32_t out = 123;
  EXPECT_EQ(ll::mbrtoc32(nullptr, "A", 1, &state), size_t(1));
  EXPECT_EQ(ll::mbrtoc32(&out, nullptr, 0, &state), size_t(0));
  EXPECT_EQ(unsigned(out), 123u);
  EXPECT_EQ(ll::mbrtoc32(&out, "\xe2", 1, &state), size_t(-2));
  EXPECT_EQ(ll::mbrtoc32(&out, nullptr, 0, &state), size_t(-1));
  EXPECT_EQ(int(libc_errno), EILSEQ);
  state = {};
  EXPECT_EQ(ll::mbrtoc16(nullptr, "\xf0\x90\x80\x80", 4, &state), size_t(4));
  EXPECT_EQ(ll::mbrtoc16(nullptr, nullptr, 0, &state), size_t(-3));
  EXPECT_NE(ll::mbsinit(&state), 0);
  char bytes[4];
  EXPECT_EQ(ll::c16rtomb(bytes, 0xd800, &state), size_t(0));
  // A null destination behaves like converting NUL, not discarding an
  // incomplete surrogate pair.
  EXPECT_EQ(ll::c16rtomb(nullptr, 'A', &state), size_t(-1));
  EXPECT_EQ(int(libc_errno), EILSEQ);
  state = {};
  EXPECT_EQ(ll::c16rtomb(nullptr, 0xd800, &state), size_t(1));
  EXPECT_NE(ll::mbsinit(&state), 0);
  EXPECT_EQ(ll::c32rtomb(nullptr, 0x110000, &state), size_t(1));
  EXPECT_EQ(ll::mbrtoc16(nullptr, "\xe2", 1, nullptr), size_t(-2));
  EXPECT_EQ(ll::mbrtoc32(&out, "A", 1, nullptr), size_t(1));
  EXPECT_EQ(ll::mbrtoc16(nullptr, "\x82\xac", 2, nullptr), size_t(2));
  EXPECT_EQ(ll::c16rtomb(bytes, 0xd800, nullptr), size_t(0));
  EXPECT_EQ(ll::c32rtomb(bytes, 'A', nullptr), size_t(1));
  EXPECT_EQ(ll::c16rtomb(bytes, 0xdc00, nullptr), size_t(4));
}

TEST(LlvmLibcUcharTest, IndependentAndCopiedStates) {
  mbstate_t first = {}, second = {};
  char32_t out = 123;
  libc_errno = EDOM;
  EXPECT_EQ(ll::mbrtoc32(&out, "\xe2", 1, &first), size_t(-2));
  EXPECT_EQ(ll::mbrtoc32(&out, "A", 1, &second), size_t(1));
  mbstate_t copy = first;
  EXPECT_EQ(ll::mbrtoc32(&out, "\x82\xac", 2, &first), size_t(2));
  EXPECT_EQ(unsigned(out), 0x20acu);
  EXPECT_EQ(ll::mbrtoc32(&out, "\x82\xac", 2, &copy), size_t(2));
  EXPECT_EQ(unsigned(out), 0x20acu);
  EXPECT_EQ(int(libc_errno), EDOM);
  char16_t unit = 123;
  EXPECT_EQ(ll::mbrtoc16(&unit, "\xf0\x90\x80\x80", 4, &first), size_t(4));
  copy = first;
  EXPECT_EQ(ll::mbrtoc16(&unit, nullptr, 1, &first), size_t(-3));
  EXPECT_EQ(unsigned(unit), 0xd800u);
  EXPECT_EQ(ll::mbrtoc16(&unit, "A", 1, &copy), size_t(-3));
  EXPECT_EQ(unsigned(unit), 0xdc00u);
  EXPECT_EQ(ll::mbrtoc16(&unit, "", 1, &copy), size_t(0));
  EXPECT_EQ(unsigned(unit), 0u);
}
