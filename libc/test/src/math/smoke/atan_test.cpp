//===-- Unittests for atan ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/math/atan.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcAtanTest = LIBC_NAMESPACE::testing::FPTest<double>;

TEST_F(LlvmLibcAtanTest, TinyRoundingAndExceptions) {
  using LIBC_NAMESPACE::fputil::testing::RoundingMode;
  const RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::Upward,
                                RoundingMode::Downward,
                                RoundingMode::TowardZero};
  const double inputs[] = {0x1p-1074, 0x1p-1023, 0x0.fffffffffffffp-1022,
                           0x1p-1022, 0x1p-1000, 0x1p-27};
  const double signs[] = {1.0, -1.0};
  for (auto mode : modes) {
    for (double x : inputs) {
      for (double sign : signs) {
        double input = sign * x;
        FPBits expected(input);
        if (mode == RoundingMode::TowardZero ||
            (mode == RoundingMode::Upward && sign < 0) ||
            (mode == RoundingMode::Downward && sign > 0))
          expected = FPBits(expected.uintval() - 1);
        int flags = FE_INEXACT;
        if (expected.get_biased_exponent() == 0)
          flags |= FE_UNDERFLOW;
        EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_MODE(
            expected.get_val(), LIBC_NAMESPACE::atan(input), flags, mode);
        if (math_errhandling & MATH_ERREXCEPT)
          EXPECT_EQ(flags, LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT));
      }
    }
  }
}

TEST_F(LlvmLibcAtanTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::atan(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ_ALL_ROUNDING(aNaN, LIBC_NAMESPACE::atan(aNaN));
  EXPECT_FP_EQ_ALL_ROUNDING(zero, LIBC_NAMESPACE::atan(zero));
  EXPECT_FP_EQ_ALL_ROUNDING(neg_zero, LIBC_NAMESPACE::atan(neg_zero));
  // atan(+-Inf) = +- pi/2.
  EXPECT_FP_EQ(0x1.921fb54442d18p0, LIBC_NAMESPACE::atan(inf));
  EXPECT_FP_EQ(-0x1.921fb54442d18p0, LIBC_NAMESPACE::atan(neg_inf));
  // atan(+-1) = +- pi/4.
  EXPECT_FP_EQ(0x1.921fb54442d18p-1, LIBC_NAMESPACE::atan(1.0));
  EXPECT_FP_EQ(-0x1.921fb54442d18p-1, LIBC_NAMESPACE::atan(-1.0));
}
