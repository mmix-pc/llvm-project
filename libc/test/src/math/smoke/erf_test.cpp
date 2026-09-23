//===-- Double-precision math regression tests ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/math_macros.h"
#include "src/math/erf.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcErfTest = LIBC_NAMESPACE::testing::FPTest<double>;

TEST_F(LlvmLibcErfTest, Tiny) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_NEAREST(
      min_denormal, LIBC_NAMESPACE::erf(min_denormal),
      FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
}

TEST_F(LlvmLibcErfTest, Saturation) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_DOWNWARD(
      0x1.fffffffffffffp-1, LIBC_NAMESPACE::erf(max_normal), FE_INEXACT);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_UPWARD(
      -0x1.fffffffffffffp-1, LIBC_NAMESPACE::erf(-max_normal), FE_INEXACT);
  EXPECT_MATH_ERRNO(0);
}

TEST_F(LlvmLibcErfTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::erf(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_ALL_ROUNDING(aNaN, LIBC_NAMESPACE::erf(aNaN));
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_ALL_ROUNDING(0.0, LIBC_NAMESPACE::erf(0.0));
  EXPECT_FP_EQ_ALL_ROUNDING(-0.0, LIBC_NAMESPACE::erf(-0.0));
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_ALL_ROUNDING(1.0, LIBC_NAMESPACE::erf(inf));
  EXPECT_FP_EQ_ALL_ROUNDING(-1.0, LIBC_NAMESPACE::erf(neg_inf));
  EXPECT_MATH_ERRNO(0);
}
