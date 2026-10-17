//===-- CPU log-gamma regression tests ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/math/lgamma.h"
#include "src/math/lgamma_r.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"

using LlvmLibcLgammaTest = LIBC_NAMESPACE::testing::FPTest<double>;

TEST_F(LlvmLibcLgammaTest, SpecialValuesAndSigns) {
  int sign = 0;
  libc_errno = 0;
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::lgamma_r(sNaN, &sign),
                              FE_INVALID);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::lgamma_r(aNaN, &sign), 0);
  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::lgamma_r(inf, &sign), 0);
  EXPECT_EQ(sign, 1);
  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::lgamma_r(neg_inf, &sign), 0);
  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::lgamma_r(zero, &sign),
                              FE_DIVBYZERO);
  EXPECT_EQ(sign, 1);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::lgamma_r(neg_zero, &sign),
                              FE_DIVBYZERO);
  EXPECT_EQ(sign, -1);
  EXPECT_MATH_ERRNO(ERANGE);
  const double poles[] = {-1.0, -2.0, -100.0, -0x1p52};
  for (double x : poles) {
    EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::lgamma_r(x, &sign),
                                FE_DIVBYZERO);
    EXPECT_MATH_ERRNO(ERANGE);
  }
  const double negative[] = {-0.5, -2.5, -100.5, -0x1p-100};
  for (double x : negative) {
    EXPECT_FP_EQ(LIBC_NAMESPACE::lgamma(x), LIBC_NAMESPACE::lgamma_r(x, &sign));
    EXPECT_EQ(sign, -1);
  }
  const double positive[] = {-1.5, -3.5, -101.5, 0.5, 4.0};
  for (double x : positive) {
    EXPECT_FP_EQ(LIBC_NAMESPACE::lgamma(x), LIBC_NAMESPACE::lgamma_r(x, &sign));
    EXPECT_EQ(sign, 1);
  }
}

TEST_F(LlvmLibcLgammaTest, ExactValues) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(zero, LIBC_NAMESPACE::lgamma(1.0),
                                           0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(zero, LIBC_NAMESPACE::lgamma(2.0),
                                           0);
}

TEST_F(LlvmLibcLgammaTest, Overflow) {
  using LIBC_NAMESPACE::fputil::testing::ForceRoundingMode;
  using LIBC_NAMESPACE::fputil::testing::RoundingMode;
  const RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::Upward,
                                RoundingMode::Downward,
                                RoundingMode::TowardZero};
  for (RoundingMode mode : modes) {
    ForceRoundingMode rounding(mode);
    if (!rounding.success)
      continue;
    double expected =
        (mode == RoundingMode::Downward || mode == RoundingMode::TowardZero)
            ? FPBits::max_normal().get_val()
            : inf;
    EXPECT_FP_EQ_WITH_EXCEPTION(
        expected, LIBC_NAMESPACE::lgamma(FPBits::max_normal().get_val()),
        FE_OVERFLOW | FE_INEXACT);
    EXPECT_MATH_ERRNO(ERANGE);
  }
}
