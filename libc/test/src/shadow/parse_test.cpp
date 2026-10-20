//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/limits.h"
#include "src/__support/integer_to_string.h"
#include "src/shadow/lookup.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::shadow_db::parse_shadow;

namespace {
int parse_number_field(unsigned field, LIBC_NAMESPACE::cpp::string_view number,
                       spwd &entry, bool append_zero = false) {
  char buffer[256] = "u:x";
  size_t length = 3;
  for (unsigned i = 0; i < 7; ++i) {
    buffer[length++] = ':';
    if (i == field) {
      for (char ch : number)
        buffer[length++] = ch;
      if (append_zero)
        buffer[length++] = '0';
    } else {
      buffer[length++] = '0';
    }
  }
  buffer[length++] = '\0';
  return parse_shadow(buffer, length, sizeof(buffer), &entry);
}
} // namespace

TEST(LlvmLibcShadowParseTest, ValuesAndSentinels) {
  char line[] = "alice:$6$salt$hash:20000:1:90:7:30:22000:42";
  spwd entry{};
  ASSERT_EQ(parse_shadow(line, sizeof(line), sizeof(line), &entry), 0);
  EXPECT_EQ(entry.sp_namp, line);
  EXPECT_EQ(entry.sp_pwdp, line + 6);
  EXPECT_STREQ(entry.sp_namp, "alice");
  EXPECT_STREQ(entry.sp_pwdp, "$6$salt$hash");
  EXPECT_EQ(entry.sp_lstchg, 20000L);
  EXPECT_EQ(entry.sp_min, 1L);
  EXPECT_EQ(entry.sp_max, 90L);
  EXPECT_EQ(entry.sp_warn, 7L);
  EXPECT_EQ(entry.sp_inact, 30L);
  EXPECT_EQ(entry.sp_expire, 22000L);
  EXPECT_EQ(entry.sp_flag, 42UL);

  char empty[] = "empty::::::::";
  ASSERT_EQ(parse_shadow(empty, sizeof(empty), sizeof(empty), &entry), 0);
  EXPECT_STREQ(entry.sp_pwdp, "");
  EXPECT_EQ(entry.sp_lstchg, -1L);
  EXPECT_EQ(entry.sp_min, -1L);
  EXPECT_EQ(entry.sp_max, -1L);
  EXPECT_EQ(entry.sp_warn, -1L);
  EXPECT_EQ(entry.sp_inact, -1L);
  EXPECT_EQ(entry.sp_expire, -1L);
  EXPECT_EQ(entry.sp_flag,
            LIBC_NAMESPACE::cpp::numeric_limits<unsigned long>::max());

  char disabled[] = "disabled:!:-1:-1:-1:-1:-1:-1:";
  ASSERT_EQ(parse_shadow(disabled, sizeof(disabled), sizeof(disabled), &entry),
            0);
  EXPECT_STREQ(entry.sp_pwdp, "!");
  EXPECT_EQ(entry.sp_lstchg, -1L);
  EXPECT_EQ(entry.sp_expire, -1L);
}

TEST(LlvmLibcShadowParseTest, DecimalBoundsInEveryField) {
  constexpr long max = LIBC_NAMESPACE::cpp::numeric_limits<long>::max();
  LIBC_NAMESPACE::IntegerToString<long> maximum(max);
  LIBC_NAMESPACE::IntegerToString<unsigned long> overflow(
      static_cast<unsigned long>(max) + 1);
  spwd entry{};
  for (unsigned i = 0; i < 6; ++i) {
    ASSERT_EQ(parse_number_field(i, maximum.view(), entry), 0);
    const long values[] = {entry.sp_lstchg, entry.sp_min,   entry.sp_max,
                           entry.sp_warn,   entry.sp_inact, entry.sp_expire};
    EXPECT_EQ(values[i], max);
    EXPECT_EQ(parse_number_field(i, overflow.view(), entry), EINVAL);
    EXPECT_EQ(parse_number_field(i, maximum.view(), entry, true), EINVAL);
  }
  constexpr unsigned long umax =
      LIBC_NAMESPACE::cpp::numeric_limits<unsigned long>::max();
  LIBC_NAMESPACE::IntegerToString<unsigned long> flag(umax);
  ASSERT_EQ(parse_number_field(6, flag.view(), entry), 0);
  EXPECT_EQ(entry.sp_flag, umax);
  EXPECT_EQ(parse_number_field(6, flag.view(), entry, true), EINVAL);
  const char *invalid[] = {"-2",
                           "+1",
                           " 1",
                           "1 ",
                           "1x",
                           "0x10",
                           "9999999999999999999999999999999999999999"};
  for (unsigned i = 0; i < 7; ++i) {
    EXPECT_EQ(parse_number_field(i, "00042", entry), 0);
    for (const char *bad : invalid)
      EXPECT_EQ(parse_number_field(i, bad, entry), EINVAL);
  }
  EXPECT_EQ(parse_number_field(6, "-1", entry), EINVAL);
}

TEST(LlvmLibcShadowParseTest, MalformedAndBoundedInput) {
  const char *invalid[] = {"", "#comment", ":x:1:2:3:4:5:6:7",
                           "u:x:1:2:3:4:5:6", "u:x:1:2:3:4:5:6:7:8"};
  for (const char *bad : invalid) {
    char buffer[128];
    size_t length = 0;
    do {
      buffer[length] = bad[length];
    } while (bad[length++]);
    spwd entry{};
    entry.sp_lstchg = 123;
    EXPECT_EQ(parse_shadow(buffer, length, sizeof(buffer), &entry), EINVAL);
    EXPECT_EQ(entry.sp_lstchg, 123L);
  }
  char buffer[] = "u:x:1:2:3:4:5:6:7";
  spwd entry{};
  EXPECT_EQ(parse_shadow(buffer, 0, sizeof(buffer), &entry), ERANGE);
  EXPECT_EQ(parse_shadow(buffer, sizeof(buffer), sizeof(buffer) - 1, &entry),
            ERANGE);
  EXPECT_EQ(parse_shadow(buffer, sizeof(buffer) - 1, sizeof(buffer), &entry),
            EINVAL);
  char embedded[] = "u:x:1:2:3:4:5:6:7\0junk";
  EXPECT_EQ(parse_shadow(embedded, sizeof(embedded), sizeof(embedded), &entry),
            EINVAL);
}
