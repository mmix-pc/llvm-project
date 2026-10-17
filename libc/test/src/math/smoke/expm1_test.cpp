//===-- Unittests for e^x - 1 ---------------------------------------------===//
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
#include "src/math/expm1.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcExpm1Test = LIBC_NAMESPACE::testing::FPTest<double>;

TEST_F(LlvmLibcExpm1Test, FiniteNearOverflow) {
  EXPECT_FP_EQ_WITH_EXCEPTION(0x1.fffffffffff2ap+1023,
                              LIBC_NAMESPACE::expm1(0x1.62e42fefa39efp+9),
                              FE_INEXACT);
  if (math_errhandling & MATH_ERREXCEPT)
    EXPECT_EQ(FE_INEXACT, LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT));
}

TEST_F(LlvmLibcExpm1Test, DirectedOverflowExceptions) {
  using LIBC_NAMESPACE::fputil::testing::RoundingMode;
  const RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::Upward,
                                RoundingMode::Downward,
                                RoundingMode::TowardZero};
  for (auto mode : modes) {
    double expected =
        (mode == RoundingMode::Downward || mode == RoundingMode::TowardZero)
            ? max_normal
            : inf;
    EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_MODE(
        expected, LIBC_NAMESPACE::expm1(0x1.62e42fefa39fp+9),
        FE_OVERFLOW | FE_INEXACT, mode);
    if (math_errhandling & MATH_ERREXCEPT)
      EXPECT_EQ(FE_OVERFLOW | FE_INEXACT,
                LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT));
  }
}

TEST_F(LlvmLibcExpm1Test, TinyResultExceptions) {
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
        if (mode == RoundingMode::Upward ||
            (mode == RoundingMode::TowardZero && sign < 0))
          expected = FPBits(expected.uintval() + (sign > 0 ? 1 : -1));
        int flags = FE_INEXACT;
        if (expected.get_biased_exponent() == 0)
          flags |= FE_UNDERFLOW;
        EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_MODE(
            expected.get_val(), LIBC_NAMESPACE::expm1(input), flags, mode);
        if (math_errhandling & MATH_ERREXCEPT)
          EXPECT_EQ(flags, LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT));
      }
    }
  }
}

TEST_F(LlvmLibcExpm1Test, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::expm1(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ(aNaN, LIBC_NAMESPACE::expm1(aNaN));
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ(inf, LIBC_NAMESPACE::expm1(inf));
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_ALL_ROUNDING(-1.0, LIBC_NAMESPACE::expm1(neg_inf));
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::expm1(0x1.0p20),
                              FE_OVERFLOW);
  EXPECT_MATH_ERRNO(ERANGE);

  EXPECT_FP_EQ_ALL_ROUNDING(zero, LIBC_NAMESPACE::expm1(zero));
  EXPECT_FP_EQ_ALL_ROUNDING(neg_zero, LIBC_NAMESPACE::expm1(neg_zero));
  // |x| < 2^-53, expm1(x) = x
  EXPECT_FP_EQ(-0x1.23456789abcdep-55,
               LIBC_NAMESPACE::expm1(-0x1.23456789abcdep-55));
  EXPECT_FP_EQ(0x1.23456789abcdep-55,
               LIBC_NAMESPACE::expm1(0x1.23456789abcdep-55));
  // log(2^-54)
  EXPECT_FP_EQ(-1.0, LIBC_NAMESPACE::expm1(-0x1.2b708872320e2p5));
}

#ifdef LIBC_TEST_FTZ_DAZ

using namespace LIBC_NAMESPACE::testing;

TEST_F(LlvmLibcExpm1Test, FTZMode) {
  ModifyMXCSR mxcsr(FTZ);

  EXPECT_FP_EQ(0.0, LIBC_NAMESPACE::expm1(min_denormal));
  EXPECT_FP_EQ(0.0, LIBC_NAMESPACE::expm1(max_denormal));
}

TEST_F(LlvmLibcExpm1Test, DAZMode) {
  ModifyMXCSR mxcsr(DAZ);

  EXPECT_FP_EQ(0.0, LIBC_NAMESPACE::expm1(min_denormal));
  EXPECT_FP_EQ(0.0, LIBC_NAMESPACE::expm1(max_denormal));
}

TEST_F(LlvmLibcExpm1Test, FTZDAZMode) {
  ModifyMXCSR mxcsr(FTZ | DAZ);

  EXPECT_FP_EQ(0.0, LIBC_NAMESPACE::expm1(min_denormal));
  EXPECT_FP_EQ(0.0, LIBC_NAMESPACE::expm1(max_denormal));
}

#endif
