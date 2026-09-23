//===-- Double-precision math regression tests ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/math_macros.h"
#include "src/math/tanh.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcTanhTest = LIBC_NAMESPACE::testing::FPTest<double>;

TEST_F(LlvmLibcTanhTest, Tiny) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_DOWNWARD(
      0.0, LIBC_NAMESPACE::tanh(min_denormal), FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_UPWARD(
      -0.0, LIBC_NAMESPACE::tanh(-min_denormal), FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
}

TEST_F(LlvmLibcTanhTest, Saturation) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_DOWNWARD(
      0x1.fffffffffffffp-1, LIBC_NAMESPACE::tanh(max_normal), FE_INEXACT);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_UPWARD(
      -0x1.fffffffffffffp-1, LIBC_NAMESPACE::tanh(-max_normal), FE_INEXACT);
  EXPECT_MATH_ERRNO(0);
}

TEST_F(LlvmLibcTanhTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::tanh(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_ALL_ROUNDING(aNaN, LIBC_NAMESPACE::tanh(aNaN));
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_ALL_ROUNDING(0.0, LIBC_NAMESPACE::tanh(0.0));
  EXPECT_FP_EQ_ALL_ROUNDING(-0.0, LIBC_NAMESPACE::tanh(-0.0));
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_ALL_ROUNDING(1.0, LIBC_NAMESPACE::tanh(inf));
  EXPECT_FP_EQ_ALL_ROUNDING(-1.0, LIBC_NAMESPACE::tanh(neg_inf));
  EXPECT_MATH_ERRNO(0);
}
