//===-- Tests for wide Bessel phases --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/math/bessel_phase.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"
#include <mpfr.h>

namespace {
void reference(mpfr_t result, double n, double x, mpfr_prec_t precision) {
  mpfr_t order, argument, root, temporary;
  mpfr_inits2(precision, order, argument, root, temporary, (mpfr_ptr)0);
  mpfr_set_d(order, n, MPFR_RNDN);
  mpfr_set_d(argument, x, MPFR_RNDN);
  mpfr_sqr(root, order, MPFR_RNDN);
  mpfr_sqr(temporary, argument, MPFR_RNDN);
  mpfr_sub(root, root, temporary, MPFR_RNDN);
  mpfr_abs(root, root, MPFR_RNDN);
  mpfr_sqrt(root, root, MPFR_RNDN);
  if (x > n) {
    mpfr_div(temporary, root, order, MPFR_RNDN);
    mpfr_atan(temporary, temporary, MPFR_RNDN);
    mpfr_mul(temporary, temporary, order, MPFR_RNDN);
    mpfr_sub(result, root, temporary, MPFR_RNDN);
  } else {
    mpfr_add(temporary, order, root, MPFR_RNDN);
    mpfr_div(temporary, temporary, argument, MPFR_RNDN);
    mpfr_log(temporary, temporary, MPFR_RNDN);
    mpfr_mul(temporary, temporary, order, MPFR_RNDN);
    mpfr_sub(result, temporary, root, MPFR_RNDN);
  }
  mpfr_clears(order, argument, root, temporary, (mpfr_ptr)0);
}

void set_wide(mpfr_t result,
              LIBC_NAMESPACE::math::bessel_phase_internal::Wide value) {
  mpfr_set_ui(result, 0, MPFR_RNDN);
  for (int shift = 96; shift >= 0; shift -= 32) {
    mpfr_mul_2ui(result, result, 32, MPFR_RNDN);
    mpfr_add_ui(result, result, static_cast<uint32_t>(value.mantissa >> shift),
                MPFR_RNDN);
  }
  mpfr_mul_2si(result, result, value.exponent, MPFR_RNDN);
  if (value.sign.is_neg())
    mpfr_neg(result, result, MPFR_RNDN);
}
} // namespace

TEST(LlvmLibcBesselPhaseTest, IndependentWideReference) {
  using Bits = LIBC_NAMESPACE::fputil::FPBits<double>;
  LIBC_NAMESPACE::fputil::testing::ForceRoundingMode guard(
      LIBC_NAMESPACE::fputil::testing::RoundingMode::Nearest);
  ASSERT_TRUE(guard.success);
  mpfr_t low, high, actual, error, limit;
  mpfr_init2(low, 512);
  mpfr_inits2(1024, high, actual, error, limit, (mpfr_ptr)0);
  const double orders[] = {2048, 4096, 0x1p20, 2147483647.0, 0x1p31};
  for (double n : orders) {
    const double points[] = {Bits::min_subnormal().get_val(),
                             0x1p-100,
                             0.5,
                             n * 0.25,
                             n * 0.75,
                             n * 0.999,
                             n,
                             n * 1.001,
                             n * 1.4142135623730951,
                             2 * n,
                             100 * n,
                             16 * n * n};
    for (double center : points) {
      for (int offset = -1; offset <= 1; ++offset) {
        double x = Bits(Bits(center).uintval() + offset).get_val();
        if (x == 0 || x > 16 * n * n)
          continue;
        auto value = LIBC_NAMESPACE::math::bessel_phase(n, x);
        reference(low, n, x, 512);
        reference(high, n, x, 1024);
        mpfr_sub(error, low, high, MPFR_RNDN);
        mpfr_abs(error, error, MPFR_RNDN);
        mpfr_set_ui_2exp(limit, 1, -300, MPFR_RNDN);
        ASSERT_TRUE(mpfr_cmp(error, limit) < 0);
        set_wide(actual, value.phase);
        mpfr_sub(error, actual, high, MPFR_RNDN);
        mpfr_abs(error, error, MPFR_RNDN);
        mpfr_set_ui_2exp(limit, 1, -54, MPFR_RNDN);
        EXPECT_TRUE(mpfr_number_p(error) && mpfr_cmp(error, limit) < 0);
        EXPECT_TRUE(value.amplitude > 0 && !Bits(value.amplitude).is_inf());
        if (x == n) {
          EXPECT_TRUE(value.zeta == 0 && value.square == 0);
        } else {
          EXPECT_EQ(value.zeta<0, x> n);
          EXPECT_EQ(value.square<0, x> n);
        }
      }
    }
  }
  mpfr_clears(low, high, actual, error, limit, (mpfr_ptr)0);
}
