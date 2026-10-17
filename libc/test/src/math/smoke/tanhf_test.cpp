//===-- Unittests for tanhf -----------------------------------------------===//
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
#include "src/math/tanhf.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcTanhfTest = LIBC_NAMESPACE::testing::FPTest<float>;

TEST_F(LlvmLibcTanhfTest, TinyResultExceptions) {
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
        if (mode == RoundingMode::TowardZero ||
            (mode == RoundingMode::Downward && sign > 0) ||
            (mode == RoundingMode::Upward && sign < 0))
          expected = FPBits(expected.uintval() - 1);
        int flags = FE_INEXACT;
        if (expected.get_biased_exponent() == 0)
          flags |= FE_UNDERFLOW;
        EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_MODE(
            expected.get_val(), LIBC_NAMESPACE::tanhf(input), flags, mode);
        if (math_errhandling & MATH_ERREXCEPT)
          EXPECT_EQ(flags, LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT));
      }
    }
  }
}

TEST_F(LlvmLibcTanhfTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::tanhf(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ(aNaN, LIBC_NAMESPACE::tanhf(aNaN));
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ(0.0f, LIBC_NAMESPACE::tanhf(0.0f));
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ(-0.0f, LIBC_NAMESPACE::tanhf(-0.0f));
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ(1.0f, LIBC_NAMESPACE::tanhf(inf));
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ(-1.0f, LIBC_NAMESPACE::tanhf(neg_inf));
  EXPECT_MATH_ERRNO(0);
}

#ifdef LIBC_TEST_FTZ_DAZ

using namespace LIBC_NAMESPACE::testing;

TEST_F(LlvmLibcTanhfTest, FTZMode) {
  ModifyMXCSR mxcsr(FTZ);

  EXPECT_FP_EQ(0.0f, LIBC_NAMESPACE::tanhf(min_denormal));
  EXPECT_FP_EQ(0.0f, LIBC_NAMESPACE::tanhf(max_denormal));
}

TEST_F(LlvmLibcTanhfTest, DAZMode) {
  ModifyMXCSR mxcsr(DAZ);

  EXPECT_FP_EQ(0.0f, LIBC_NAMESPACE::tanhf(min_denormal));
  EXPECT_FP_EQ(0.0f, LIBC_NAMESPACE::tanhf(max_denormal));
}

TEST_F(LlvmLibcTanhfTest, FTZDAZMode) {
  ModifyMXCSR mxcsr(FTZ | DAZ);

  EXPECT_FP_EQ(0.0f, LIBC_NAMESPACE::tanhf(min_denormal));
  EXPECT_FP_EQ(0.0f, LIBC_NAMESPACE::tanhf(max_denormal));
}

#endif
