//===-- Unittests for atanhf ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/math_macros.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/math/atanhf.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

#include "hdr/stdint_proxy.h"

using LIBC_NAMESPACE::Sign;

using LlvmLibcAtanhfTest = LIBC_NAMESPACE::testing::FPTest<float>;

TEST_F(LlvmLibcAtanhfTest, TinyResultExceptions) {
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
        if ((mode == RoundingMode::Upward && sign > 0) ||
            (mode == RoundingMode::Downward && sign < 0))
          expected = FPBits(expected.uintval() + 1);
        int flags = FE_INEXACT;
        if (expected.get_biased_exponent() == 0)
          flags |= FE_UNDERFLOW;
        EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_MODE(
            expected.get_val(), LIBC_NAMESPACE::atanhf(input), flags, mode);
        if (math_errhandling & MATH_ERREXCEPT)
          EXPECT_EQ(flags, LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT));
      }
    }
  }
}

TEST_F(LlvmLibcAtanhfTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::atanhf(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);
  // TODO: Strengthen errno,exception checks and remove these assert macros
  // after new matchers/test fixtures are added, see:
  // https://github.com/llvm/llvm-project/issues/90653
  LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
  EXPECT_FP_EQ_ALL_ROUNDING(aNaN, LIBC_NAMESPACE::atanhf(aNaN));
  // TODO: Uncomment these checks later, RoundingMode affects running
  // tests in this way https://github.com/llvm/llvm-project/issues/90653.
  // EXPECT_FP_EXCEPTION(0);
  EXPECT_MATH_ERRNO(0);

  LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
  EXPECT_FP_EQ_ALL_ROUNDING(0.0f, LIBC_NAMESPACE::atanhf(0.0f));
  // See above TODO
  // EXPECT_FP_EXCEPTION(0);
  EXPECT_MATH_ERRNO(0);

  LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
  EXPECT_FP_EQ_ALL_ROUNDING(-0.0f, LIBC_NAMESPACE::atanhf(-0.0f));
  // See above TODO
  // EXPECT_FP_EXCEPTION(0);
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::atanhf(1.0f), FE_DIVBYZERO);
  EXPECT_MATH_ERRNO(ERANGE);

  EXPECT_FP_EQ_WITH_EXCEPTION(neg_inf, LIBC_NAMESPACE::atanhf(-1.0f),
                              FE_DIVBYZERO);
  EXPECT_MATH_ERRNO(ERANGE);

  auto bt = FPBits(1.0f);
  bt.set_uintval(bt.uintval() + 1);

  EXPECT_FP_IS_NAN_WITH_EXCEPTION(LIBC_NAMESPACE::atanhf(bt.get_val()),
                                  FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);

  bt.set_sign(Sign::NEG);
  EXPECT_FP_IS_NAN_WITH_EXCEPTION(LIBC_NAMESPACE::atanhf(bt.get_val()),
                                  FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);

  EXPECT_FP_IS_NAN_WITH_EXCEPTION(LIBC_NAMESPACE::atanhf(2.0f), FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);

  EXPECT_FP_IS_NAN_WITH_EXCEPTION(LIBC_NAMESPACE::atanhf(-2.0f), FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);

  EXPECT_FP_IS_NAN_WITH_EXCEPTION(LIBC_NAMESPACE::atanhf(inf), FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);

  bt.set_sign(Sign::NEG);
  EXPECT_FP_IS_NAN_WITH_EXCEPTION(LIBC_NAMESPACE::atanhf(neg_inf), FE_INVALID);
  EXPECT_MATH_ERRNO(EDOM);
}

#ifdef LIBC_TEST_FTZ_DAZ

using namespace LIBC_NAMESPACE::testing;

TEST_F(LlvmLibcAtanhfTest, FTZMode) {
  ModifyMXCSR mxcsr(FTZ);

  EXPECT_FP_EQ(0.0f, LIBC_NAMESPACE::atanhf(min_denormal));
}

TEST_F(LlvmLibcAtanhfTest, DAZMode) {
  ModifyMXCSR mxcsr(DAZ);

  EXPECT_FP_EQ(0.0f, LIBC_NAMESPACE::atanhf(min_denormal));
}

TEST_F(LlvmLibcAtanhfTest, FTZDAZMode) {
  ModifyMXCSR mxcsr(FTZ | DAZ);

  EXPECT_FP_EQ(0.0f, LIBC_NAMESPACE::atanhf(min_denormal));
}

#endif
