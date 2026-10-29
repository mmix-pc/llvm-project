//===-- Tests for uniform integer Bessel evaluation -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/math/bessel_hankel.h"
#include "src/__support/math/bessel_uniform.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"
#include <mpfr.h>

TEST(LlvmLibcBesselUniformTest, IndependentReference) {
  using Bits = LIBC_NAMESPACE::fputil::FPBits<double>;
  LIBC_NAMESPACE::fputil::testing::ForceRoundingMode guard(
      LIBC_NAMESPACE::fputil::testing::RoundingMode::Nearest);
  ASSERT_TRUE(guard.success);
  mpfr_t input, order, low, high, actual, error, limit, temporary, phase;
  mpfr_init2(low, 256);
  mpfr_inits2(512, input, order, high, actual, error, limit, temporary, phase,
              (mpfr_ptr)0);
  const long orders[] = {2048, 4096};
  for (long n : orders) {
    double cbn = LIBC_NAMESPACE::math::cbrt(double(n));
    const double points[] = {0.5,
                             0.75 * double(n),
                             double(n) - 3 * cbn,
                             double(n) - 0.71 * cbn,
                             double(n) + 0.71 * cbn,
                             double(n) + 3 * cbn,
                             double(n) * 1.25};
    for (double x : points) {
      auto result = LIBC_NAMESPACE::math::bessel_uniform(double(n), x);
      const double values[] = {result.j, result.y};
      mpfr_set_ui(phase, 0, MPFR_RNDN);
      for (int shift = 96; shift >= 0; shift -= 32) {
        mpfr_mul_2ui(phase, phase, 32, MPFR_RNDN);
        mpfr_add_ui(phase, phase,
                    static_cast<uint32_t>(result.phase.mantissa >> shift),
                    MPFR_RNDN);
      }
      mpfr_mul_2si(phase, phase, result.phase.exponent, MPFR_RNDN);
      if (result.phase.sign.is_neg())
        mpfr_neg(phase, phase, MPFR_RNDN);
      for (unsigned kind = 0; kind < 2; ++kind) {
        mpfr_set_d(input, x, MPFR_RNDN);
        mpfr_set_si(order, n, MPFR_RNDN);
        auto function = kind ? mpfr_yn : mpfr_jn;
        function(low, n, input, MPFR_RNDN);
        function(high, n, input, MPFR_RNDN);
        mpfr_exp_t exponent = mpfr_get_exp(high);
        mpfr_mul_2si(low, low, -exponent, MPFR_RNDN);
        mpfr_mul_2si(temporary, high, -exponent, MPFR_RNDN);
        ASSERT_EQ(Bits(mpfr_get_d(low, MPFR_RNDN)).uintval(),
                  Bits(mpfr_get_d(temporary, MPFR_RNDN)).uintval());
        mpfr_set_d(actual, values[kind], MPFR_RNDN);
        if (result.scaled) {
          mpfr_exp(temporary, phase, MPFR_RNDN);
          if (kind)
            mpfr_mul(actual, actual, temporary, MPFR_RNDN);
          else
            mpfr_div(actual, actual, temporary, MPFR_RNDN);
        }
        mpfr_abs(limit, high, MPFR_RNDN);
        if (x >= double(n)) {
          mpfr_cbrt(temporary, order, MPFR_RNDN);
          mpfr_ui_div(temporary, 1, temporary, MPFR_RNDN);
          mpfr_sqr(error, input, MPFR_RNDN);
          mpfr_sqr(low, order, MPFR_RNDN);
          mpfr_sub(error, error, low, MPFR_RNDN);
          mpfr_sqrt(error, error, MPFR_RNDN);
          mpfr_sqrt(error, error, MPFR_RNDN);
          mpfr_ui_div(error, 1, error, MPFR_RNDN);
          mpfr_min(temporary, temporary, error, MPFR_RNDN);
          mpfr_max(limit, limit, temporary, MPFR_RNDN);
        }
        mpfr_mul_2si(limit, limit, -40, MPFR_RNDN);
        mpfr_sub(error, actual, high, MPFR_RNDN);
        mpfr_abs(error, error, MPFR_RNDN);
        if (!mpfr_number_p(error) || mpfr_cmp(error, limit) > 0)
          mpfr_printf("n=%ld x=%Ra kind=%u error=%Ra limit=%Ra\n", n, input,
                      kind, error, limit);
        EXPECT_TRUE(mpfr_number_p(error) && mpfr_cmp(error, limit) <= 0);
      }
    }
  }
  mpfr_clears(input, order, low, high, actual, error, limit, temporary, phase,
              (mpfr_ptr)0);
}

TEST(LlvmLibcBesselUniformTest, HankelPartitionOverlap) {
  LIBC_NAMESPACE::fputil::testing::ForceRoundingMode guard(
      LIBC_NAMESPACE::fputil::testing::RoundingMode::Nearest);
  ASSERT_TRUE(guard.success);
  const unsigned orders[] = {2048,     2049,       4096,
                             0x100000, 0x7fffffff, 0x80000000};
  for (unsigned n : orders) {
    double x = 16.0 * double(n) * double(n);
    auto uniform = LIBC_NAMESPACE::math::bessel_uniform(double(n), x);
    auto hankel = LIBC_NAMESPACE::math::bessel_hankel(n, x);
    double scale = 0x1p-42 / LIBC_NAMESPACE::math::sqrt(x);
    EXPECT_TRUE(uniform.j - hankel.j < scale && hankel.j - uniform.j < scale);
    EXPECT_TRUE(uniform.y - hankel.y < scale && hankel.y - uniform.y < scale);
  }
}
