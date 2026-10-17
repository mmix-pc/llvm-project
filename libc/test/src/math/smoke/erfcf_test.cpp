//===-- Exceptional-value math regression tests ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/math_macros.h"
#include "src/math/erfcf.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcErfcfTest = LIBC_NAMESPACE::testing::FPTest<float>;

TEST_F(LlvmLibcErfcfTest, NaNs) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::erfcf(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::erfcf(aNaN), 0);
  EXPECT_MATH_ERRNO(0);
}

TEST_F(LlvmLibcErfcfTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(1.0f, LIBC_NAMESPACE::erfcf(0.0f), 0);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(1.0f, LIBC_NAMESPACE::erfcf(-0.0f), 0);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(0.0f, LIBC_NAMESPACE::erfcf(inf), 0);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(2.0f, LIBC_NAMESPACE::erfcf(neg_inf), 0);
  EXPECT_MATH_ERRNO(0);
}

TEST_F(LlvmLibcErfcfTest, TailAndTinyDirected) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_UPWARD(min_denormal,
                                              LIBC_NAMESPACE::erfcf(max_normal),
                                              FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_DOWNWARD(
      0.0f, LIBC_NAMESPACE::erfcf(max_normal), FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_DOWNWARD(
      0x1.fffffep0f, LIBC_NAMESPACE::erfcf(-max_normal), FE_INEXACT);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_DOWNWARD(
      0x1.fffffep-1f, LIBC_NAMESPACE::erfcf(min_denormal), FE_INEXACT);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_UPWARD(
      0x1.000002p0f, LIBC_NAMESPACE::erfcf(-min_denormal), FE_INEXACT);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_UPWARD(
      1.0f, LIBC_NAMESPACE::erfcf(min_denormal), FE_INEXACT);
  EXPECT_MATH_ERRNO(0);
}
