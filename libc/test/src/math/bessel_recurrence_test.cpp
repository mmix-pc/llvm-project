//===-- Tests for scaled integer Bessel recurrences -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/__support/math/bessel_recurrence.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"
#include <mpfr.h>

TEST(LlvmLibcBesselRecurrenceTest, IndependentScaledReference) {
  namespace fp = LIBC_NAMESPACE::fputil;
  using Bits = fp::FPBits<double>;
  fp::testing::ForceRoundingMode guard(fp::testing::RoundingMode::Nearest);
  ASSERT_TRUE(guard.success);
  mpfr_t input, order, low, high, actual, error, scale, amplitude, temporary;
  mpfr_init2(low, 256);
  mpfr_inits2(512, input, order, high, actual, error, scale, amplitude,
              temporary, (mpfr_ptr)0);
  const unsigned orders[] = {2, 3, 17, 64, 512, 2048};
  for (unsigned n : orders) {
    const double points[] = {Bits::min_subnormal().get_val(),
                             0x1p-100,
                             0.5,
                             double(n) - 0.5,
                             double(n),
                             double(n) + 0.5,
                             2.0 * double(n)};
    for (double x : points) {
      libc_errno = 0;
      fp::clear_except(FE_ALL_EXCEPT);
      auto result = LIBC_NAMESPACE::math::bessel_recurrence(n, x);
      EXPECT_EQ(int(libc_errno), 0);
      EXPECT_EQ(fp::test_except(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW |
                                FE_UNDERFLOW),
                0);
      const LIBC_NAMESPACE::math::BesselFloat values[] = {result.j, result.y};
      for (unsigned kind = 0; kind < 2; ++kind) {
        mpfr_set_d(input, x, MPFR_RNDN);
        mpfr_set_ui(order, n, MPFR_RNDN);
        auto function = kind ? mpfr_yn : mpfr_jn;
        function(low, static_cast<long>(n), input, MPFR_RNDN);
        function(high, static_cast<long>(n), input, MPFR_RNDN);
        // Compare the reference significand, not a binary64 overflow/zero.
        mpfr_exp_t reference_exponent = mpfr_get_exp(high);
        mpfr_mul_2si(low, low, -reference_exponent, MPFR_RNDN);
        mpfr_mul_2si(temporary, high, -reference_exponent, MPFR_RNDN);
        ASSERT_EQ(Bits(mpfr_get_d(low, MPFR_RNDN)).uintval(),
                  Bits(mpfr_get_d(temporary, MPFR_RNDN)).uintval());
        auto normalized = values[kind];
        ASSERT_FALSE(normalized.mantissa.is_zero());
        int exponent = normalized.get_unbiased_exponent();
        normalized.exponent -= exponent;
        mpfr_set_d(actual, double(normalized), MPFR_RNDN);
        mpfr_mul_2si(actual, actual, exponent, MPFR_RNDN);
        mpfr_abs(scale, high, MPFR_RNDN);
        if (x >= double(n)) {
          mpfr_cbrt(amplitude, order, MPFR_RNDN);
          mpfr_ui_div(amplitude, 1, amplitude, MPFR_RNDN);
          if (x > double(n)) {
            mpfr_sqr(temporary, input, MPFR_RNDN);
            mpfr_sqr(error, order, MPFR_RNDN);
            mpfr_sub(temporary, temporary, error, MPFR_RNDN);
            mpfr_sqrt(temporary, temporary, MPFR_RNDN);
            mpfr_sqrt(temporary, temporary, MPFR_RNDN);
            mpfr_ui_div(temporary, 1, temporary, MPFR_RNDN);
            mpfr_min(amplitude, amplitude, temporary, MPFR_RNDN);
          }
          mpfr_max(scale, scale, amplitude, MPFR_RNDN);
        }
        mpfr_mul_2si(scale, scale, -40, MPFR_RNDN);
        mpfr_sub(error, actual, high, MPFR_RNDN);
        mpfr_abs(error, error, MPFR_RNDN);
        if (!mpfr_number_p(error) || mpfr_cmp(error, scale) > 0)
          mpfr_printf("n=%u x=%Ra kind=%u error=%Ra limit=%Ra\n", n, input,
                      kind, error, scale);
        EXPECT_TRUE(mpfr_number_p(error) && mpfr_cmp(error, scale) <= 0);
      }
    }
  }
  mpfr_clears(input, order, low, high, actual, error, scale, amplitude,
              temporary, (mpfr_ptr)0);
}
