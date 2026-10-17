//===-- Focused math regression tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "hdr/stdint_proxy.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/math/erfc.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"
#include "utils/MPFRWrapper/MPFRUtils.h"

using LlvmLibcErfcTest = LIBC_NAMESPACE::testing::FPTest<double>;
namespace mpfr = LIBC_NAMESPACE::testing::mpfr;

// Regression thresholds, not an exhaustive correct-rounding claim.
TEST_F(LlvmLibcErfcTest, Boundaries) {
  const double inputs[] = {
      min_denormal, min_normal, 0x1p-56, 0.25, 0.84375, 1.25, 0x1.6db6dp+1,
      6.0,          26.0,       26.5,    27.0, 27.25,   28.0, max_normal};
  for (double center : inputs) {
    for (int offset = -16; offset <= 16; ++offset) {
      uint64_t bits = FPBits(center).uintval();
      if (offset < 0 && bits < uint64_t(-offset))
        continue;
      double x = FPBits(uint64_t(bits + offset)).get_val();
      if (FPBits(x).is_inf() || FPBits(x).is_nan())
        continue;
      for (int sign = -1; sign <= 1; sign += 2) {
        double v = sign * x;
        EXPECT_MPFR_MATCH(mpfr::Operation::Erfc, v, LIBC_NAMESPACE::erfc(v),
                          2.0);
        EXPECT_MPFR_MATCH_ALL_ROUNDING(mpfr::Operation::Erfc, v,
                                       LIBC_NAMESPACE::erfc(v), 3.0);
      }
    }
  }
}

TEST_F(LlvmLibcErfcTest, SampledRange) {
  constexpr uint64_t COUNT = 4096;
  const uint64_t STEP = (0x7fefffffffffffffULL) / COUNT;
  for (uint64_t i = 0; i <= COUNT; ++i) {
    double samples[] = {FPBits(uint64_t(i * STEP)).get_val(),
                        28.0 * double(i) / COUNT};
    for (double x : samples) {
      for (int sign = -1; sign <= 1; sign += 2) {
        double v = sign * x;
        ASSERT_MPFR_MATCH(mpfr::Operation::Erfc, v, LIBC_NAMESPACE::erfc(v),
                          2.0);
        ASSERT_MPFR_MATCH_ALL_ROUNDING(mpfr::Operation::Erfc, v,
                                       LIBC_NAMESPACE::erfc(v), 3.0);
      }
    }
  }
}

TEST_F(LlvmLibcErfcTest, DirectedRegression) {
  // Inputs from the initial non-FMA directed-rounding failures. Keep them
  // independent of SampledRange, whose assertions stop at the first failure.
  constexpr uint64_t INPUTS[] = {0x3fea780000000000ULL, 0x3feafffffffffff6ULL,
                                 0x3ff4000000000000ULL, 0x3ff400000000000fULL,
                                 0x4006db6cfffffff3ULL, 0x4006db6d0000000aULL,
                                 0x4018000000000003ULL, 0x403a80000000000aULL};
  for (uint64_t bits : INPUTS) {
    double x = FPBits(bits).get_val();
    for (int sign = -1; sign <= 1; sign += 2) {
      double v = sign < 0 ? -x : x;
      EXPECT_MPFR_MATCH(mpfr::Operation::Erfc, v, LIBC_NAMESPACE::erfc(v), 2.0);
      EXPECT_MPFR_MATCH_ALL_ROUNDING(mpfr::Operation::Erfc, v,
                                     LIBC_NAMESPACE::erfc(v), 3.0);
    }
  }
}
