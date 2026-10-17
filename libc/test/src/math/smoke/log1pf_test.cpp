//===-- Unittests for log1pf ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/math_macros.h"
#include "hdr/stdint_proxy.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/math/log1pf.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcLog1pfTest = LIBC_NAMESPACE::testing::FPTest<float>;

TEST_F(LlvmLibcLog1pfTest, TinyResultExceptions) {
  using LIBC_NAMESPACE::fputil::testing::RoundingMode;
  const RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::Upward,
                                RoundingMode::Downward,
                                RoundingMode::TowardZero};
  const float inputs[] = {min_denormal, max_denormal, min_normal, 0x1p-60};
  const float signs[] = {1, -1};
  for (auto mode : modes) {
    for (float x : inputs) {
      for (float sign : signs) {
        float input = sign * x;
        FPBits expected(input);
        if (mode == RoundingMode::Downward ||
            (mode == RoundingMode::TowardZero && sign > 0))
          expected = FPBits(expected.uintval() + (sign < 0 ? 1 : -1));
        int flags = FE_INEXACT;
        if (expected.get_biased_exponent() == 0)
          flags |= FE_UNDERFLOW;
        EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_MODE(
            expected.get_val(), LIBC_NAMESPACE::log1pf(input), flags, mode);
        if (math_errhandling & MATH_ERREXCEPT)
          EXPECT_EQ(flags, LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT));
      }
    }
  }
}

TEST_F(LlvmLibcLog1pfTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::log1pf(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ(aNaN, LIBC_NAMESPACE::log1pf(aNaN));
  EXPECT_FP_EQ(inf, LIBC_NAMESPACE::log1pf(inf));
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_IS_NAN_WITH_EXCEPTION(LIBC_NAMESPACE::log1pf(neg_inf), FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);
  EXPECT_FP_EQ(zero, LIBC_NAMESPACE::log1pf(0.0f));
  EXPECT_FP_EQ(neg_zero, LIBC_NAMESPACE::log1pf(-0.0f));
  EXPECT_FP_EQ_WITH_EXCEPTION(neg_inf, LIBC_NAMESPACE::log1pf(-1.0f),
                              FE_DIVBYZERO);
  EXPECT_MATH_ERRNO(ERANGE);
}

#ifdef LIBC_TEST_FTZ_DAZ

using namespace LIBC_NAMESPACE::testing;

TEST_F(LlvmLibcLog1pfTest, FTZMode) {
  ModifyMXCSR mxcsr(FTZ);

  EXPECT_FP_EQ(0.0f, LIBC_NAMESPACE::log1pf(min_denormal));
}

TEST_F(LlvmLibcLog1pfTest, DAZMode) {
  ModifyMXCSR mxcsr(DAZ);

  EXPECT_FP_EQ(0.0f, LIBC_NAMESPACE::log1pf(min_denormal));
}

TEST_F(LlvmLibcLog1pfTest, FTZDAZMode) {
  ModifyMXCSR mxcsr(FTZ | DAZ);

  EXPECT_FP_EQ(0.0f, LIBC_NAMESPACE::log1pf(min_denormal));
}

#endif
