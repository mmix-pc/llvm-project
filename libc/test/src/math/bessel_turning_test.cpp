//===-- Tests for the Bessel turning-region kernel ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/math/bessel_turning.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"
#include <mpfr.h>

TEST(LlvmLibcBesselTurningTest, IndependentIntegerOrderReference) {
  using Bits = LIBC_NAMESPACE::fputil::FPBits<double>;
  LIBC_NAMESPACE::fputil::testing::ForceRoundingMode guard(
      LIBC_NAMESPACE::fputil::testing::RoundingMode::Nearest);
  ASSERT_TRUE(guard.success);
  mpfr_t input, order, low, high, actual, scale, difference, amplitude,
      temporary;
  mpfr_inits2(256, low, (mpfr_ptr)0);
  mpfr_inits2(512, input, order, high, actual, scale, difference, amplitude,
              temporary, (mpfr_ptr)0);
  const long orders[] = {2048, 4096};
  // MPFR's integer-order reference is expensive near the turning point.
  // Keep the dispatch edges, center and both immediate grid neighbors here.
  const int samples[] = {-14, -1, 0, 1, 14};
  for (long n : orders) {
    double root = LIBC_NAMESPACE::math::cbrt(double(n));
    for (int k : samples) {
      double x = double(n) + double(k) * root / 20.0;
      auto values = LIBC_NAMESPACE::math::bessel_turning(double(n), x);
      const double results[] = {values.j, values.y};
      for (unsigned second_kind = 0; second_kind < 2; ++second_kind) {
        mpfr_set_d(input, x, MPFR_RNDN);
        mpfr_set_si(order, n, MPFR_RNDN);
        auto function = second_kind ? mpfr_yn : mpfr_jn;
        function(low, n, input, MPFR_RNDN);
        function(high, n, input, MPFR_RNDN);
        ASSERT_EQ(Bits(mpfr_get_d(low, MPFR_RNDN)).uintval(),
                  Bits(mpfr_get_d(high, MPFR_RNDN)).uintval());
        mpfr_abs(scale, high, MPFR_RNDN);
        if (x >= double(n)) {
          mpfr_cbrt(amplitude, order, MPFR_RNDN);
          mpfr_ui_div(amplitude, 1, amplitude, MPFR_RNDN);
          if (x > double(n)) {
            mpfr_sqr(temporary, input, MPFR_RNDN);
            mpfr_sqr(difference, order, MPFR_RNDN);
            mpfr_sub(temporary, temporary, difference, MPFR_RNDN);
            mpfr_sqrt(temporary, temporary, MPFR_RNDN);
            mpfr_sqrt(temporary, temporary, MPFR_RNDN);
            mpfr_ui_div(temporary, 1, temporary, MPFR_RNDN);
            mpfr_min(amplitude, amplitude, temporary, MPFR_RNDN);
          }
          mpfr_max(scale, scale, amplitude, MPFR_RNDN);
        }
        mpfr_mul_2si(scale, scale, -40, MPFR_RNDN);
        mpfr_set_d(actual, results[second_kind], MPFR_RNDN);
        mpfr_sub(difference, actual, high, MPFR_RNDN);
        mpfr_abs(difference, difference, MPFR_RNDN);
        if (!mpfr_number_p(difference) || mpfr_cmp(difference, scale) > 0)
          mpfr_printf("order=%ld x=%Ra kind=%u actual=%Ra reference=%Ra "
                      "error=%Ra limit=%Ra\n",
                      n, input, second_kind, actual, high, difference, scale);
        EXPECT_TRUE(mpfr_number_p(difference) &&
                    mpfr_cmp(difference, scale) <= 0);
      }
    }
  }
  mpfr_clears(input, order, low, high, actual, scale, difference, amplitude,
              temporary, (mpfr_ptr)0);
}
