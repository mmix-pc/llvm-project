//===-- Unittests for asin ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/fenv_macros.h"
#include "src/math/asin.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcAsinTest = LIBC_NAMESPACE::testing::FPTest<double>;

TEST_F(LlvmLibcAsinTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(aNaN, LIBC_NAMESPACE::asin(sNaN),
                                           FE_INVALID);
  EXPECT_FP_EQ_ALL_ROUNDING(aNaN, LIBC_NAMESPACE::asin(aNaN));
  EXPECT_FP_EQ_ALL_ROUNDING(zero, LIBC_NAMESPACE::asin(zero));
  EXPECT_FP_EQ_ALL_ROUNDING(neg_zero, LIBC_NAMESPACE::asin(neg_zero));
  EXPECT_FP_EQ_ALL_ROUNDING(aNaN, LIBC_NAMESPACE::asin(inf));
  EXPECT_FP_EQ_ALL_ROUNDING(aNaN, LIBC_NAMESPACE::asin(neg_inf));
  EXPECT_FP_EQ_ALL_ROUNDING(aNaN, LIBC_NAMESPACE::asin(2.0));
  EXPECT_FP_EQ_ALL_ROUNDING(aNaN, LIBC_NAMESPACE::asin(-2.0));
  EXPECT_FP_EQ(0x1.921fb54442d18p0, LIBC_NAMESPACE::asin(1.0));
  EXPECT_FP_EQ(-0x1.921fb54442d18p0, LIBC_NAMESPACE::asin(-1.0));
}

TEST_F(LlvmLibcAsinTest, TinyRoundingAndExceptions) {
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
        if ((mode == RoundingMode::Upward && sign > 0) ||
            (mode == RoundingMode::Downward && sign < 0))
          expected = FPBits(expected.uintval() + 1);
        int flags = FE_INEXACT;
        if (expected.get_biased_exponent() == 0)
          flags |= FE_UNDERFLOW;
        EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_MODE(
            expected.get_val(), LIBC_NAMESPACE::asin(input), flags, mode);
        if (math_errhandling & MATH_ERREXCEPT)
          EXPECT_EQ(flags, LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT));
      }
    }
  }
}

#ifdef LIBC_TEST_FTZ_DAZ

using namespace LIBC_NAMESPACE::testing;

// With FTZ/DAZ flags are set, when the inputs are denormal, the output bit
// pattern might not be exactly 0, but they should all be equal to 0 as floating
// points.

TEST_F(LlvmLibcAsinTest, FTZMode) {
  ModifyMXCSR mxcsr(FTZ);

  EXPECT_TRUE(zero == LIBC_NAMESPACE::asin(min_denormal));
}

TEST_F(LlvmLibcAsinTest, DAZMode) {
  ModifyMXCSR mxcsr(DAZ);

  EXPECT_TRUE(zero == LIBC_NAMESPACE::asin(min_denormal));
}

TEST_F(LlvmLibcAsinTest, FTZDAZMode) {
  ModifyMXCSR mxcsr(FTZ | DAZ);

  EXPECT_TRUE(zero == LIBC_NAMESPACE::asin(min_denormal));
}

#endif
