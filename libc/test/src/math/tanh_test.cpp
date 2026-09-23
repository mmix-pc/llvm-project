//===-- Double-precision math regression tests ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/math_macros.h"
#include "src/math/tanh.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"
#include "utils/MPFRWrapper/MPFRUtils.h"

using LlvmLibcTanhTest = LIBC_NAMESPACE::testing::FPTest<double>;
namespace mpfr = LIBC_NAMESPACE::testing::mpfr;

// The fdlibm-based composition is not a correctly-rounded implementation.
// Keep an explicit two-ULP nearest / three-ULP directed error contract.
TEST_F(LlvmLibcTanhTest, Boundaries) {
  const double inputs[] = {
      min_denormal, max_denormal, min_normal,   0x1p-1015,
      0x1p-55,      0x1p-28,      0x1p-27,      0x1.62e43p-2,
      0.84375,      1.0,          1.25,         0x1.6db6ep+1,
      6.0,          22.0,         0x1.62e42p+9, 0x1.633ce8fb9f87dp+9};
  for (double center : inputs) {
    for (int offset = -16; offset <= 16; ++offset) {
      uint64_t bits = FPBits(center).uintval();
      if (offset < 0 && bits < uint64_t(-offset))
        continue;
      double x = FPBits(bits + offset).get_val();
      for (int sign = -1; sign <= 1; sign += 2) {
        double v = sign * x;
        EXPECT_MPFR_MATCH_ALL_ROUNDING(mpfr::Operation::Tanh, v,
                                       LIBC_NAMESPACE::tanh(v), 3.0);
        EXPECT_MPFR_MATCH(mpfr::Operation::Tanh, v, LIBC_NAMESPACE::tanh(v),
                          2.0);
      }
    }
  }
}

TEST_F(LlvmLibcTanhTest, SampledRange) {
  // Cover both dense ordinary inputs and the entire binary64 exponent range.
  constexpr uint64_t COUNT = 10000;
  constexpr uint64_t STEP = 0x7fefffffffffffffULL / COUNT;
  for (uint64_t i = 0; i <= COUNT; ++i) {
    double samples[] = {FPBits(i * STEP).get_val(), 22.0 * double(i) / COUNT};
    for (double x : samples)
      for (int sign = -1; sign <= 1; sign += 2) {
        double v = sign * x;
        ASSERT_MPFR_MATCH_ALL_ROUNDING(mpfr::Operation::Tanh, v,
                                       LIBC_NAMESPACE::tanh(v), 3.0);
        ASSERT_MPFR_MATCH(mpfr::Operation::Tanh, v, LIBC_NAMESPACE::tanh(v),
                          2.0);
      }
  }
}
