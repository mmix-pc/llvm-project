//===-- Tests for getsubopt -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/getsubopt.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcGetsuboptTest, ValuesAndExactProgress) {
  char a[] = "a", ab[] = "ab", b[] = "b";
  char *const keys[] = {a, ab, b, nullptr};
  char input[] = "a,ab=,b=one=two,a=last";
  char *next = input;
  char *value = input;
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::getsubopt(&next, keys, &value), 0);
  EXPECT_EQ(value, nullptr);
  EXPECT_EQ(next, input + 2);
  EXPECT_STREQ(input, "a");
  EXPECT_EQ(LIBC_NAMESPACE::getsubopt(&next, keys, &value), 1);
  EXPECT_EQ(value, input + 5);
  EXPECT_STREQ(value, "");
  EXPECT_EQ(next, input + 6);
  EXPECT_EQ(LIBC_NAMESPACE::getsubopt(&next, keys, &value), 2);
  EXPECT_EQ(value, input + 8);
  EXPECT_STREQ(value, "one=two");
  EXPECT_EQ(next, input + 16);
  EXPECT_EQ(LIBC_NAMESPACE::getsubopt(&next, keys, &value), 0);
  EXPECT_EQ(value, input + 18);
  EXPECT_STREQ(value, "last");
  EXPECT_EQ(next, input + sizeof(input) - 1);
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_STREQ(a, "a");
  EXPECT_STREQ(ab, "ab");
  EXPECT_STREQ(b, "b");
  EXPECT_EQ(keys[0], a);
  EXPECT_EQ(keys[1], ab);
  EXPECT_EQ(keys[2], b);
  EXPECT_EQ(keys[3], nullptr);
}

TEST(LlvmLibcGetsuboptTest, UnknownValuesAndExactTokenMatch) {
  char a[] = "a", ab[] = "ab";
  char *const keys[] = {a, ab, nullptr};
  char input[] = "abc=payload,unknown,=empty,,ab,";
  char *next = input;
  char *value = nullptr;
  EXPECT_EQ(LIBC_NAMESPACE::getsubopt(&next, keys, &value), -1);
  EXPECT_STREQ(value, "payload");
  EXPECT_STREQ(input, "abc=payload");
  EXPECT_EQ(LIBC_NAMESPACE::getsubopt(&next, keys, &value), -1);
  EXPECT_EQ(value, nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::getsubopt(&next, keys, &value), -1);
  EXPECT_STREQ(value, "empty");
  EXPECT_EQ(LIBC_NAMESPACE::getsubopt(&next, keys, &value), -1);
  EXPECT_EQ(value, nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::getsubopt(&next, keys, &value), 1);
  EXPECT_EQ(value, nullptr);
  EXPECT_EQ(next, input + sizeof(input) - 1);
}

TEST(LlvmLibcGetsuboptTest, EmptyInputAndKeylist) {
  char *const keys[] = {nullptr};
  char empty[] = "";
  char *next = empty;
  char *value = empty;
  EXPECT_EQ(LIBC_NAMESPACE::getsubopt(&next, keys, &value), -1);
  EXPECT_EQ(next, empty);
  EXPECT_EQ(value, nullptr);
  char input[] = "missing=1";
  next = input;
  EXPECT_EQ(LIBC_NAMESPACE::getsubopt(&next, keys, &value), -1);
  EXPECT_STREQ(value, "1");
  EXPECT_EQ(next, input + sizeof(input) - 1);
}
