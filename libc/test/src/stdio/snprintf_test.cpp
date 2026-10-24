//===-- Unittests for snprintf --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/snprintf.h"

#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcSNPrintfTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

// The sprintf test cases cover testing the shared printf functionality, so
// these tests will focus on snprintf exclusive features.

TEST(LlvmLibcSNPrintfTest, CutOff) {
  char buff[100];
  int written;

  written = LIBC_NAMESPACE::snprintf(buff, 16,
                                     "A simple string with no conversions.");
  EXPECT_EQ(written, 36);
  ASSERT_STREQ(buff, "A simple string");

  written = LIBC_NAMESPACE::snprintf(buff, 5, "%s", "1234567890");
  EXPECT_EQ(written, 10);
  ASSERT_STREQ(buff, "1234");

  written = LIBC_NAMESPACE::snprintf(buff, 67, "%-101c", 'a');
  EXPECT_EQ(written, 101);
  ASSERT_STREQ(buff, "a "
                     "        " // Each of these is 8 spaces, and there are 8.
                     "        " // In total there are 65 spaces
                     "        " // 'a' + 65 spaces + '\0' = 67
                     "        "
                     "        "
                     "        "
                     "        "
                     "        ");

  // passing null as the output pointer is allowed as long as buffsz is 0.
  written = LIBC_NAMESPACE::snprintf(nullptr, 0, "%s and more", "1234567890");
  EXPECT_EQ(written, 19);
}

TEST_F(LlvmLibcSNPrintfTest, MinimumDynamicWidthOverflows) {
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(nullptr, 0, "%*s", INT_MIN, ""), -1);
  ASSERT_ERRNO_EQ(EOVERFLOW);
}

#ifndef LIBC_COPT_PRINTF_DISABLE_FLOAT
TEST_F(LlvmLibcSNPrintfTest, HexPrecisionOverflow) {
  // Count-only calls must reject the complete field before padding billions
  // of zeroes, even though the destination discards every character.
  const int precisions[] = {INT_MAX, INT_MAX - 1, INT_MAX - 6};
  const char *formats[] = {"%.*a", "%.*A", "%-#.*a", "%+020.*A"};
  const char *long_formats[] = {"%.*La", "%.*LA"};
  for (int precision : precisions) {
    for (const char *format : formats) {
      EXPECT_LT(LIBC_NAMESPACE::snprintf(nullptr, 0, format, precision, 1.0), 0);
      ASSERT_ERRNO_EQ(EOVERFLOW);
    }
    for (const char *format : long_formats) {
      EXPECT_LT(LIBC_NAMESPACE::snprintf(nullptr, 0, format, precision, 1.0L),
                0);
      ASSERT_ERRNO_EQ(EOVERFLOW);
    }
  }

  // The field alone fits exactly in INT_MAX; the preceding byte does not.
  EXPECT_LT(
      LIBC_NAMESPACE::snprintf(nullptr, 0, "x%.*a", INT_MAX - 7, 1.0), 0);
  ASSERT_ERRNO_EQ(EOVERFLOW);
  EXPECT_LT(LIBC_NAMESPACE::snprintf(nullptr, 0, "x%*.*a", INT_MAX, 0, 1.0), 0);
  ASSERT_ERRNO_EQ(EOVERFLOW);
}

TEST_F(LlvmLibcSNPrintfTest, HexPrecisionControls) {
  char buf[32];
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(buf, sizeof(buf), "%.*a", INT_MIN, 1.0),
            6);
  EXPECT_STREQ(buf, "0x1p+0");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(buf, sizeof(buf), "%.*a", 0, 1.0), 6);
  EXPECT_STREQ(buf, "0x1p+0");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(buf, sizeof(buf), "%.*a", 1, 1.5), 8);
  EXPECT_STREQ(buf, "0x1.8p+0");
  // Non-finite conversions ignore precision, including INT_MAX.
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(buf, sizeof(buf), "%.*a", INT_MAX,
                                   __builtin_inf()),
            3);
  EXPECT_STREQ(buf, "inf");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(buf, sizeof(buf), "%.*A", INT_MAX,
                                   __builtin_nan("")),
            3);
  EXPECT_STREQ(buf, "NAN");
  ASSERT_ERRNO_SUCCESS();
}
#endif

TEST(LlvmLibcSNPrintfTest, NoCutOff) {
  char buff[64];
  int written;

  written = LIBC_NAMESPACE::snprintf(buff, 37,
                                     "A simple string with no conversions.");
  EXPECT_EQ(written, 36);
  ASSERT_STREQ(buff, "A simple string with no conversions.");

  written = LIBC_NAMESPACE::snprintf(buff, 20, "%s", "1234567890");
  EXPECT_EQ(written, 10);
  ASSERT_STREQ(buff, "1234567890");
}

TEST(LlvmLibcSNPrintfTest, CharsWrittenOverflow) {
  char buff[0];

  // Trigger an overflow in the return value of snprintf by writing more than
  // INT_MAX bytes.
  int int_max = LIBC_NAMESPACE::cpp::numeric_limits<int>::max();
  int written = LIBC_NAMESPACE::snprintf(buff, 0, "%*stest", int_max, "");
  EXPECT_LT(written, 0);
  ASSERT_ERRNO_FAILURE();
}
