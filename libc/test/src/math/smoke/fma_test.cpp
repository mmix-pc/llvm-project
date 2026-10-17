//===-- Unittests for fma ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FmaTest.h"

#include "src/math/fma.h"

LIST_FMA_TESTS(double, LIBC_NAMESPACE::fma)

TEST_F(LlvmLibcFmaTest, NegativeUnderflowWithPositiveZero) {
  using LIBC_NAMESPACE::fputil::testing::RoundingMode;
  const RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::TowardZero,
                                RoundingMode::Upward};
  for (auto mode : modes)
    EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_MODE(
        -0.0, LIBC_NAMESPACE::fma(-0x1p-1000, 0x1p-100, 0.0),
        FE_UNDERFLOW | FE_INEXACT, mode);
}

TEST_F(LlvmLibcFmaTest, FiniteResultFromLargeOperands) {
  using LIBC_NAMESPACE::fputil::testing::RoundingMode;
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_MODE(
      0x1.b93aac527380fp+1023,
      LIBC_NAMESPACE::fma(0x1.139ea77b760ecp+1022, 0x1.9b25f2fd017eap+2,
                          -0x1.bc16788922b6fp+1023),
      FE_INEXACT, RoundingMode::Nearest);
  if (math_errhandling & MATH_ERREXCEPT)
    EXPECT_EQ(FE_INEXACT, LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT));
}
