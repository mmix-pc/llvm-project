//===-- Double-precision math regression tests ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/math_macros.h"
#include "src/math/sinh.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcSinhTest = LIBC_NAMESPACE::testing::FPTest<double>;

TEST_F(LlvmLibcSinhTest, Tiny) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_UPWARD(
      2 * min_denormal, LIBC_NAMESPACE::sinh(min_denormal),
      FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_DOWNWARD(
      -2 * min_denormal, LIBC_NAMESPACE::sinh(-min_denormal),
      FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
}

TEST_F(LlvmLibcSinhTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::sinh(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_ALL_ROUNDING(aNaN, LIBC_NAMESPACE::sinh(aNaN));
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_ALL_ROUNDING(0.0, LIBC_NAMESPACE::sinh(0.0));
  EXPECT_FP_EQ_ALL_ROUNDING(-0.0, LIBC_NAMESPACE::sinh(-0.0));
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_ALL_ROUNDING(inf, LIBC_NAMESPACE::sinh(inf));
  EXPECT_FP_EQ_ALL_ROUNDING(neg_inf, LIBC_NAMESPACE::sinh(neg_inf));
  EXPECT_MATH_ERRNO(0);
}

TEST_F(LlvmLibcSinhTest, Overflow) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_DOWNWARD(
      max_normal, LIBC_NAMESPACE::sinh(max_normal), FE_OVERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_TOWARD_ZERO(
      -max_normal, LIBC_NAMESPACE::sinh(-max_normal), FE_OVERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::sinh(0x1.633ce8fb9f87ep+9),
                              FE_OVERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION(neg_inf, LIBC_NAMESPACE::sinh(-max_normal),
                              FE_OVERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
}
