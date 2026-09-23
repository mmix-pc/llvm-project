//===-- Double-precision math regression tests ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/math_macros.h"
#include "src/math/cosh.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcCoshTest = LIBC_NAMESPACE::testing::FPTest<double>;

TEST_F(LlvmLibcCoshTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::cosh(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_ALL_ROUNDING(aNaN, LIBC_NAMESPACE::cosh(aNaN));
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_ALL_ROUNDING(1.0, LIBC_NAMESPACE::cosh(0.0));
  EXPECT_FP_EQ_ALL_ROUNDING(1.0, LIBC_NAMESPACE::cosh(-0.0));
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_ALL_ROUNDING(inf, LIBC_NAMESPACE::cosh(inf));
  EXPECT_FP_EQ_ALL_ROUNDING(inf, LIBC_NAMESPACE::cosh(neg_inf));
  EXPECT_MATH_ERRNO(0);
}

TEST_F(LlvmLibcCoshTest, Overflow) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_DOWNWARD(
      max_normal, LIBC_NAMESPACE::cosh(max_normal), FE_OVERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_TOWARD_ZERO(
      max_normal, LIBC_NAMESPACE::cosh(-max_normal), FE_OVERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::cosh(0x1.633ce8fb9f87ep+9),
                              FE_OVERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::cosh(-max_normal),
                              FE_OVERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
}
