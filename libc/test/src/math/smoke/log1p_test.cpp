//===-- Unittests for log1p -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/math_macros.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/math/log1p.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcLog1pTest = LIBC_NAMESPACE::testing::FPTest<double>;

TEST_F(LlvmLibcLog1pTest, TinyResultExceptions) {
  using LIBC_NAMESPACE::fputil::testing::RoundingMode;
  const RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::Upward,
                                RoundingMode::Downward,
                                RoundingMode::TowardZero};
  const double inputs[] = {min_denormal, max_denormal, min_normal, 0x1p-60};
  const double signs[] = {1, -1};
  for (auto mode : modes) {
    for (double x : inputs) {
      for (double sign : signs) {
        double input = sign * x;
        FPBits expected(input);
        if (mode == RoundingMode::Downward ||
            (mode == RoundingMode::TowardZero && sign > 0))
          expected = FPBits(expected.uintval() + (sign < 0 ? 1 : -1));
        int flags = FE_INEXACT;
        if (expected.get_biased_exponent() == 0)
          flags |= FE_UNDERFLOW;
        EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_MODE(
            expected.get_val(), LIBC_NAMESPACE::log1p(input), flags, mode);
        if (math_errhandling & MATH_ERREXCEPT)
          EXPECT_EQ(flags, LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT));
      }
    }
  }
}

TEST_F(LlvmLibcLog1pTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::log1p(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ(aNaN, LIBC_NAMESPACE::log1p(aNaN));
  EXPECT_FP_EQ(inf, LIBC_NAMESPACE::log1p(inf));
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_IS_NAN_WITH_EXCEPTION(LIBC_NAMESPACE::log1p(neg_inf), FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);
  EXPECT_FP_IS_NAN_WITH_EXCEPTION(LIBC_NAMESPACE::log1p(-2.0), FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);
  EXPECT_FP_EQ(zero, LIBC_NAMESPACE::log1p(0.0));
  EXPECT_FP_EQ(neg_zero, LIBC_NAMESPACE::log1p(-0.0));
  EXPECT_FP_EQ_WITH_EXCEPTION(neg_inf, LIBC_NAMESPACE::log1p(-1.0),
                              FE_DIVBYZERO);
  EXPECT_MATH_ERRNO(ERANGE);

  EXPECT_FP_EQ(0x1.62c829bf8fd9dp9,
               LIBC_NAMESPACE::log1p(0x1.9b536cac3a09dp1023));
}

#ifdef LIBC_TEST_FTZ_DAZ

using namespace LIBC_NAMESPACE::testing;

TEST_F(LlvmLibcLog1pTest, FTZMode) {
  ModifyMXCSR mxcsr(FTZ);

  EXPECT_FP_EQ(0.0, LIBC_NAMESPACE::log1p(min_denormal));
  EXPECT_FP_EQ(0x1.62c829bf8fd9dp9,
               LIBC_NAMESPACE::log1p(0x1.9b536cac3a09dp1023));
}

TEST_F(LlvmLibcLog1pTest, DAZMode) {
  ModifyMXCSR mxcsr(DAZ);

  EXPECT_FP_EQ(0.0, LIBC_NAMESPACE::log1p(min_denormal));
  EXPECT_FP_EQ(0x1.62c829bf8fd9dp9,
               LIBC_NAMESPACE::log1p(0x1.9b536cac3a09dp1023));
}

TEST_F(LlvmLibcLog1pTest, FTZDAZMode) {
  ModifyMXCSR mxcsr(FTZ | DAZ);

  EXPECT_FP_EQ(0.0, LIBC_NAMESPACE::log1p(min_denormal));
  EXPECT_FP_EQ(0x1.62c829bf8fd9dp9,
               LIBC_NAMESPACE::log1p(0x1.9b536cac3a09dp1023));
}

#endif
