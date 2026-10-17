//===-- Unittests for exp -------------------------------------------------===//
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
#include "src/math/exp.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcExpTest = LIBC_NAMESPACE::testing::FPTest<double>;

TEST_F(LlvmLibcExpTest, InexactSubnormal) {
  EXPECT_FP_EQ_WITH_EXCEPTION(min_denormal,
                              LIBC_NAMESPACE::exp(-0x1.74910d52d3051p+9),
                              FE_UNDERFLOW | FE_INEXACT);
}

TEST_F(LlvmLibcExpTest, DirectedOverflowExceptions) {
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
        expected, LIBC_NAMESPACE::exp(0x1.62e42fefa39fp+9),
        FE_OVERFLOW | FE_INEXACT, mode);
    if (math_errhandling & MATH_ERREXCEPT)
      EXPECT_EQ(FE_OVERFLOW | FE_INEXACT,
                LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT));
  }
}

TEST_F(LlvmLibcExpTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::exp(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ(aNaN, LIBC_NAMESPACE::exp(aNaN));
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ(inf, LIBC_NAMESPACE::exp(inf));
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_ALL_ROUNDING(zero, LIBC_NAMESPACE::exp(neg_inf));
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ_WITH_EXCEPTION(zero, LIBC_NAMESPACE::exp(-0x1.0p20),
                              FE_UNDERFLOW);
  EXPECT_MATH_ERRNO(ERANGE);
  EXPECT_FP_EQ_WITH_EXCEPTION(inf, LIBC_NAMESPACE::exp(0x1.0p20), FE_OVERFLOW);
  EXPECT_MATH_ERRNO(ERANGE);

  EXPECT_FP_EQ_ALL_ROUNDING(1.0, LIBC_NAMESPACE::exp(0.0));
  EXPECT_FP_EQ_ALL_ROUNDING(1.0, LIBC_NAMESPACE::exp(-0.0));
}

#ifdef LIBC_TEST_FTZ_DAZ

using namespace LIBC_NAMESPACE::testing;

TEST_F(LlvmLibcExpTest, FTZMode) {
  ModifyMXCSR mxcsr(FTZ);

  EXPECT_FP_EQ(1.0, LIBC_NAMESPACE::exp(min_denormal));
  EXPECT_FP_EQ(1.0, LIBC_NAMESPACE::exp(max_denormal));
}

TEST_F(LlvmLibcExpTest, DAZMode) {
  ModifyMXCSR mxcsr(DAZ);

  EXPECT_FP_EQ(1.0, LIBC_NAMESPACE::exp(min_denormal));
  EXPECT_FP_EQ(1.0, LIBC_NAMESPACE::exp(max_denormal));
}

TEST_F(LlvmLibcExpTest, FTZDAZMode) {
  ModifyMXCSR mxcsr(FTZ | DAZ);

  EXPECT_FP_EQ(1.0, LIBC_NAMESPACE::exp(min_denormal));
  EXPECT_FP_EQ(1.0, LIBC_NAMESPACE::exp(max_denormal));
}

#endif
