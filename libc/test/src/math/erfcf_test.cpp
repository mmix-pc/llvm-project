//===-- Focused math regression tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "hdr/stdint_proxy.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/math/erfcf.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"
#include "utils/MPFRWrapper/MPFRUtils.h"

using LlvmLibcErfcfTest = LIBC_NAMESPACE::testing::FPTest<float>;
namespace mpfr = LIBC_NAMESPACE::testing::mpfr;

// Regression thresholds, not an exhaustive correct-rounding claim.
TEST_F(LlvmLibcErfcfTest, Boundaries) {
  const float inputs[] = {min_denormal, min_normal, 0x1p-26f,  0.25f,
                          0.84375f,     1.25f,      4.0f,      9.0f,
                          10.0f,        11.0f,      max_normal};
  for (float center : inputs) {
    for (int offset = -16; offset <= 16; ++offset) {
      uint32_t bits = FPBits(center).uintval();
      if (offset < 0 && bits < uint32_t(-offset))
        continue;
      float x = FPBits(uint32_t(bits + offset)).get_val();
      if (FPBits(x).is_inf() || FPBits(x).is_nan())
        continue;
      for (int sign = -1; sign <= 1; sign += 2) {
        float v = sign < 0 ? -x : x;
        EXPECT_MPFR_MATCH(mpfr::Operation::Erfc, v, LIBC_NAMESPACE::erfcf(v),
                          0.5);
        EXPECT_MPFR_MATCH_ALL_ROUNDING(mpfr::Operation::Erfc, v,
                                       LIBC_NAMESPACE::erfcf(v), 0.5);
      }
    }
  }
}

TEST_F(LlvmLibcErfcfTest, SampledRange) {
  constexpr uint32_t COUNT = 4096;
  const uint32_t STEP = (0x7f7fffffU) / COUNT;
  for (uint32_t i = 0; i <= COUNT; ++i) {
    float samples[] = {FPBits(uint32_t(i * STEP)).get_val(),
                       11.0f * float(i) / COUNT};
    for (float x : samples) {
      for (int sign = -1; sign <= 1; sign += 2) {
        float v = sign < 0 ? -x : x;
        ASSERT_MPFR_MATCH(mpfr::Operation::Erfc, v, LIBC_NAMESPACE::erfcf(v),
                          0.5);
        ASSERT_MPFR_MATCH_ALL_ROUNDING(mpfr::Operation::Erfc, v,
                                       LIBC_NAMESPACE::erfcf(v), 0.5);
      }
    }
  }
}
