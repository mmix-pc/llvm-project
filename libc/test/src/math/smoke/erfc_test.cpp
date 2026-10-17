//===-- Exceptional-value math regression tests ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/math_macros.h"
#include "src/math/erfc.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcErfcTest = LIBC_NAMESPACE::testing::FPTest<double>;

TEST_F(LlvmLibcErfcTest, NaNs) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::erfc(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::erfc(aNaN), 0);
  EXPECT_MATH_ERRNO(0);
}

TEST_F(LlvmLibcErfcTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(1.0, LIBC_NAMESPACE::erfc(0.0), 0);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(1.0, LIBC_NAMESPACE::erfc(-0.0), 0);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(0.0, LIBC_NAMESPACE::erfc(inf), 0);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(2.0, LIBC_NAMESPACE::erfc(neg_inf), 0);
  EXPECT_MATH_ERRNO(0);
}

TEST_F(LlvmLibcErfcTest, TailAndTinyDirected) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_UPWARD(min_denormal,
                                              LIBC_NAMESPACE::erfc(max_normal),
                                              FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_DOWNWARD(
      0.0, LIBC_NAMESPACE::erfc(max_normal), FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_DOWNWARD(
      0x1.fffffffffffffp0, LIBC_NAMESPACE::erfc(-max_normal), FE_INEXACT);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_DOWNWARD(
      0x1.fffffffffffffp-1, LIBC_NAMESPACE::erfc(min_denormal), FE_INEXACT);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_UPWARD(
      0x1.0000000000001p0, LIBC_NAMESPACE::erfc(-min_denormal), FE_INEXACT);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_UPWARD(
      1.0, LIBC_NAMESPACE::erfc(min_denormal), FE_INEXACT);
  EXPECT_MATH_ERRNO(0);
}
