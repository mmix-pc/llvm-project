//===-- Tests for the math comparison macros ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "include/llvm-libc-macros/math-function-macros.h"
#include "test/UnitTest/LibcCTest.h"

TEST(math_comparison) {
  EXPECT_TRUE(isgreater(2.0f, 1.0));
  EXPECT_TRUE(isgreaterequal(2.0L, 2.0f));
  EXPECT_TRUE(isless(-1.0, 0.0L));
  EXPECT_TRUE(islessequal(-0.0f, 0.0L));
  EXPECT_TRUE(islessgreater(-1.0L, 1.0));
  EXPECT_FALSE(islessgreater(-0.0, 0.0));
  EXPECT_FALSE(isunordered(1.0f, 1.0L));
  EXPECT_TRUE(isunordered(NAN, 1.0));
  EXPECT_TRUE(isunordered(1.0, NAN));
  EXPECT_FALSE(isgreater(NAN, 1.0));
  EXPECT_FALSE(isgreaterequal(1.0, NAN));
  EXPECT_FALSE(isless(NAN, 1.0));
  EXPECT_FALSE(islessequal(1.0, NAN));
  EXPECT_FALSE(islessgreater(NAN, NAN));

#define CHECK_ONCE(macro)                                                      \
  do {                                                                         \
    double x = 1.0, y = 2.0;                                                   \
    (void)macro(x++, y++);                                                     \
    EXPECT_TRUE(x == 2.0);                                                     \
    EXPECT_TRUE(y == 3.0);                                                     \
  } while (0)
  CHECK_ONCE(isgreater);
  CHECK_ONCE(isgreaterequal);
  CHECK_ONCE(isless);
  CHECK_ONCE(islessequal);
  CHECK_ONCE(islessgreater);
  CHECK_ONCE(isunordered);
#undef CHECK_ONCE
}
