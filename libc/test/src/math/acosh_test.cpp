//===-- Focused math regression tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "hdr/stdint_proxy.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/math/acosh.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"
#include "utils/MPFRWrapper/MPFRUtils.h"

using LlvmLibcAcoshTest = LIBC_NAMESPACE::testing::FPTest<double>;
namespace mpfr = LIBC_NAMESPACE::testing::mpfr;

// Regression thresholds, not an exhaustive correct-rounding claim.
TEST_F(LlvmLibcAcoshTest, Boundaries) {
  const double inputs[] = {1.0, 2.0, 0x1p28, 0x1p512, max_normal};
  for (double center : inputs) {
    for (int offset = -16; offset <= 16; ++offset) {
      uint64_t bits = FPBits(center).uintval();
      if (offset < 0 && bits < uint64_t(-offset))
        continue;
      double x = FPBits(uint64_t(bits + offset)).get_val();
      if (FPBits(x).is_inf() || FPBits(x).is_nan() || x < 1.0)
        continue;
      for (int sign = 1; sign <= 1; sign += 2) {
        double v = sign * x;
        EXPECT_MPFR_MATCH(mpfr::Operation::Acosh, v, LIBC_NAMESPACE::acosh(v),
                          2.0);
        EXPECT_MPFR_MATCH_ALL_ROUNDING(mpfr::Operation::Acosh, v,
                                       LIBC_NAMESPACE::acosh(v), 3.0);
      }
    }
  }
}

TEST_F(LlvmLibcAcoshTest, SampledRange) {
  constexpr uint64_t COUNT = 4096;
  const uint64_t STEP = (0x7fefffffffffffffULL - FPBits(1.0).uintval()) / COUNT;
  for (uint64_t i = 0; i <= COUNT; ++i) {
    double samples[] = {
        FPBits(uint64_t(FPBits(1.0).uintval() + i * STEP)).get_val(),
        1.0 + 10.0 * double(i) / COUNT};
    for (double x : samples) {
      for (int sign = 1; sign <= 1; sign += 2) {
        double v = sign * x;
        ASSERT_MPFR_MATCH(mpfr::Operation::Acosh, v, LIBC_NAMESPACE::acosh(v),
                          2.0);
        ASSERT_MPFR_MATCH_ALL_ROUNDING(mpfr::Operation::Acosh, v,
                                       LIBC_NAMESPACE::acosh(v), 3.0);
      }
    }
  }
}
