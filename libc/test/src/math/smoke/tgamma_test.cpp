//===-- CPU tgamma regression tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/math/tgamma.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"

using LlvmLibcGammaTest = LIBC_NAMESPACE::testing::FPTest<double>;
using LIBC_NAMESPACE::fputil::testing::ForceRoundingMode;
using LIBC_NAMESPACE::fputil::testing::RoundingMode;

TEST_F(LlvmLibcGammaTest, SpecialValues) {
  libc_errno = 0;
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::tgamma(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::tgamma(aNaN), 0);
  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::tgamma(inf), 0);
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::tgamma(neg_inf),
                              FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);
  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::tgamma(zero), FE_DIVBYZERO);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION(neg_inf, LIBC_NAMESPACE::tgamma(neg_zero),
                              FE_DIVBYZERO);
  EXPECT_MATH_ERRNO(ERANGE);
  const double poles[] = {-1.0, -2.0, -100.0, -0x1p52};
  for (double x : poles) {
    EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::tgamma(x), FE_INVALID);
    EXPECT_MATH_ERRNO(EDOM);
  }
}

TEST_F(LlvmLibcGammaTest, ExactFactorials) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(
      double(1), LIBC_NAMESPACE::tgamma(double(1)), 0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(
      double(1), LIBC_NAMESPACE::tgamma(double(2)), 0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(
      double(2), LIBC_NAMESPACE::tgamma(double(3)), 0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(
      double(24), LIBC_NAMESPACE::tgamma(double(5)), 0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(
      double(362880), LIBC_NAMESPACE::tgamma(double(10)), 0);
}

TEST_F(LlvmLibcGammaTest, DirectedRangeErrors) {
  const RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::Upward,
                                RoundingMode::Downward,
                                RoundingMode::TowardZero};
  for (RoundingMode mode : modes) {
    ForceRoundingMode rounding(mode);
    if (!rounding.success)
      continue;
    double positive_overflow =
        (mode == RoundingMode::Downward || mode == RoundingMode::TowardZero)
            ? FPBits::max_normal().get_val()
            : inf;
    double negative_overflow =
        (mode == RoundingMode::Upward || mode == RoundingMode::TowardZero)
            ? FPBits::max_normal(Sign::NEG).get_val()
            : neg_inf;
    const double overflow_inputs[] = {172.0, 200.0,
                                      FPBits::min_subnormal().get_val()};
    for (double x : overflow_inputs) {
      EXPECT_FP_EQ_WITH_EXCEPTION(positive_overflow, LIBC_NAMESPACE::tgamma(x),
                                  FE_OVERFLOW | FE_INEXACT);
      EXPECT_MATH_ERRNO(ERANGE);
    }
    EXPECT_FP_EQ_WITH_EXCEPTION(
        negative_overflow,
        LIBC_NAMESPACE::tgamma(-FPBits::min_subnormal().get_val()),
        FE_OVERFLOW | FE_INEXACT);
    EXPECT_MATH_ERRNO(ERANGE);
    double positive_tiny =
        mode == RoundingMode::Upward ? FPBits::min_subnormal().get_val() : zero;
    double negative_tiny = mode == RoundingMode::Downward
                               ? FPBits::min_subnormal(Sign::NEG).get_val()
                               : neg_zero;
    EXPECT_FP_EQ_WITH_EXCEPTION(positive_tiny,
                                LIBC_NAMESPACE::tgamma(double(-201.5)),
                                FE_UNDERFLOW | FE_INEXACT);
    EXPECT_MATH_ERRNO(ERANGE);
    EXPECT_FP_EQ_WITH_EXCEPTION(negative_tiny,
                                LIBC_NAMESPACE::tgamma(double(-200.5)),
                                FE_UNDERFLOW | FE_INEXACT);
    EXPECT_MATH_ERRNO(ERANGE);
  }
}

TEST_F(LlvmLibcGammaTest, PreserveErrnoOnSuccess) {
  libc_errno = EDOM;
  EXPECT_FP_EQ(double(24), LIBC_NAMESPACE::tgamma(double(5)));
  EXPECT_MATH_ERRNO(EDOM);
}

TEST_F(LlvmLibcGammaTest, NonzeroSubnormal) {
  ForceRoundingMode rounding(RoundingMode::Nearest);
  if (!rounding.success)
    return;
  double expected = LIBC_NAMESPACE::tgamma(-175.5);
  EXPECT_TRUE(FPBits(expected).is_subnormal());
  EXPECT_FALSE(FPBits(expected).is_zero());
  EXPECT_FP_EQ_WITH_EXCEPTION(expected, LIBC_NAMESPACE::tgamma(-175.5),
                              FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
}
