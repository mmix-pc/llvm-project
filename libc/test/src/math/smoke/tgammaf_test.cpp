//===-- CPU tgammaf regression tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/math/tgammaf.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"

using LlvmLibcGammaTest = LIBC_NAMESPACE::testing::FPTest<float>;
using LIBC_NAMESPACE::fputil::testing::ForceRoundingMode;
using LIBC_NAMESPACE::fputil::testing::RoundingMode;

TEST_F(LlvmLibcGammaTest, SpecialValues) {
  libc_errno = 0;
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::tgammaf(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::tgammaf(aNaN), 0);
  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::tgammaf(inf), 0);
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::tgammaf(neg_inf),
                              FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);
  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::tgammaf(zero), FE_DIVBYZERO);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION(neg_inf, LIBC_NAMESPACE::tgammaf(neg_zero),
                              FE_DIVBYZERO);
  EXPECT_MATH_ERRNO(ERANGE);
  const float poles[] = {-1.0f, -2.0f, -100.0f, -0x1p52f};
  for (float x : poles) {
    EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::tgammaf(x), FE_INVALID);
    EXPECT_MATH_ERRNO(EDOM);
  }
}

TEST_F(LlvmLibcGammaTest, ExactFactorials) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(
      float(1), LIBC_NAMESPACE::tgammaf(float(1)), 0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(
      float(1), LIBC_NAMESPACE::tgammaf(float(2)), 0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(
      float(2), LIBC_NAMESPACE::tgammaf(float(3)), 0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(
      float(24), LIBC_NAMESPACE::tgammaf(float(5)), 0);
  EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(
      float(362880), LIBC_NAMESPACE::tgammaf(float(10)), 0);
}

TEST_F(LlvmLibcGammaTest, DirectedRangeErrors) {
  const RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::Upward,
                                RoundingMode::Downward,
                                RoundingMode::TowardZero};
  for (RoundingMode mode : modes) {
    ForceRoundingMode rounding(mode);
    if (!rounding.success)
      continue;
    float positive_overflow =
        (mode == RoundingMode::Downward || mode == RoundingMode::TowardZero)
            ? FPBits::max_normal().get_val()
            : inf;
    float negative_overflow =
        (mode == RoundingMode::Upward || mode == RoundingMode::TowardZero)
            ? FPBits::max_normal(Sign::NEG).get_val()
            : neg_inf;
    const float overflow_inputs[] = {172.0f, 200.0f,
                                     FPBits::min_subnormal().get_val()};
    for (float x : overflow_inputs) {
      EXPECT_FP_EQ_WITH_EXCEPTION(positive_overflow, LIBC_NAMESPACE::tgammaf(x),
                                  FE_OVERFLOW | FE_INEXACT);
      EXPECT_MATH_ERRNO(ERANGE);
    }
    EXPECT_FP_EQ_WITH_EXCEPTION(
        negative_overflow,
        LIBC_NAMESPACE::tgammaf(-FPBits::min_subnormal().get_val()),
        FE_OVERFLOW | FE_INEXACT);
    EXPECT_MATH_ERRNO(ERANGE);
    float positive_tiny =
        mode == RoundingMode::Upward ? FPBits::min_subnormal().get_val() : zero;
    float negative_tiny = mode == RoundingMode::Downward
                              ? FPBits::min_subnormal(Sign::NEG).get_val()
                              : neg_zero;
    EXPECT_FP_EQ_WITH_EXCEPTION(positive_tiny,
                                LIBC_NAMESPACE::tgammaf(float(-201.5)),
                                FE_UNDERFLOW | FE_INEXACT);
    EXPECT_MATH_ERRNO(ERANGE);
    EXPECT_FP_EQ_WITH_EXCEPTION(negative_tiny,
                                LIBC_NAMESPACE::tgammaf(float(-200.5)),
                                FE_UNDERFLOW | FE_INEXACT);
    EXPECT_MATH_ERRNO(ERANGE);
  }
}

TEST_F(LlvmLibcGammaTest, PreserveErrnoOnSuccess) {
  libc_errno = EDOM;
  EXPECT_FP_EQ(float(24), LIBC_NAMESPACE::tgammaf(float(5)));
  EXPECT_MATH_ERRNO(EDOM);
}

TEST_F(LlvmLibcGammaTest, NonzeroSubnormal) {
  ForceRoundingMode rounding(RoundingMode::Nearest);
  if (!rounding.success)
    return;
  float expected = LIBC_NAMESPACE::tgammaf(-35.5f);
  EXPECT_TRUE(FPBits(expected).is_subnormal());
  EXPECT_FALSE(FPBits(expected).is_zero());
  EXPECT_FP_EQ_WITH_EXCEPTION(expected, LIBC_NAMESPACE::tgammaf(-35.5f),
                              FE_UNDERFLOW | FE_INEXACT);
  EXPECT_MATH_ERRNO(ERANGE);
}
