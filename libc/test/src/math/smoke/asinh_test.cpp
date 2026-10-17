//===-- Exceptional-value math regression tests ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/math_macros.h"
#include "src/math/asinh.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcAsinhTest = LIBC_NAMESPACE::testing::FPTest<double>;

TEST_F(LlvmLibcAsinhTest, NaNs) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::asinh(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::asinh(aNaN), 0);
  EXPECT_MATH_ERRNO(0);
}

TEST_F(LlvmLibcAsinhTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(0.0, LIBC_NAMESPACE::asinh(0.0), 0);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(-0.0, LIBC_NAMESPACE::asinh(-0.0), 0);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::asinh(inf), 0);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(neg_inf, LIBC_NAMESPACE::asinh(neg_inf), 0);
  EXPECT_MATH_ERRNO(0);
}

TEST_F(LlvmLibcAsinhTest, TinyDirected) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_DOWNWARD(
      0.0, LIBC_NAMESPACE::asinh(min_denormal), FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_UPWARD(
      -0.0, LIBC_NAMESPACE::asinh(-min_denormal), FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
}
