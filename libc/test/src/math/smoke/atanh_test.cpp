//===-- Exceptional-value math regression tests ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/math_macros.h"
#include "src/math/atanh.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcAtanhTest = LIBC_NAMESPACE::testing::FPTest<double>;

TEST_F(LlvmLibcAtanhTest, NaNs) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::atanh(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::atanh(aNaN), 0);
  EXPECT_MATH_ERRNO(0);
}

TEST_F(LlvmLibcAtanhTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(0.0, LIBC_NAMESPACE::atanh(0.0), 0);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(-0.0, LIBC_NAMESPACE::atanh(-0.0), 0);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::atanh(1.0), FE_DIVBYZERO);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION(neg_inf, LIBC_NAMESPACE::atanh(-1.0),
                              FE_DIVBYZERO);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::atanh(0x1.0000000000001p0),
                              FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::atanh(-0x1.0000000000001p0),
                              FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::atanh(inf), FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::atanh(neg_inf), FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);
}

TEST_F(LlvmLibcAtanhTest, TinyDirected) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_UPWARD(
      0x1p-1073, LIBC_NAMESPACE::atanh(min_denormal),
      FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_DOWNWARD(
      -0x1p-1073, LIBC_NAMESPACE::atanh(-min_denormal),
      FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
}
