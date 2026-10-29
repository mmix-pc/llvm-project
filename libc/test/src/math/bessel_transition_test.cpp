//===-- Tests for stable Bessel transition variables
//-----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/math/bessel_transition.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"
#include <mpfr.h>

namespace {
using Bits = LIBC_NAMESPACE::fputil::FPBits<double>;

void reference(double n, double x, mpfr_prec_t precision, double (&result)[3]) {
  mpfr_t z, square, w, phase, zeta, amplitude, temporary;
  mpfr_inits2(precision, z, square, w, phase, zeta, amplitude, temporary,
              (mpfr_ptr)0);
  mpfr_set_d(z, x, MPFR_RNDN);
  mpfr_div_d(z, z, n, MPFR_RNDN);
  mpfr_sqr(square, z, MPFR_RNDN);
  mpfr_ui_sub(square, 1, square, MPFR_RNDN);
  mpfr_abs(w, square, MPFR_RNDN);
  mpfr_sqrt(w, w, MPFR_RNDN);
  if (x < n) {
    mpfr_add_ui(temporary, w, 1, MPFR_RNDN);
    mpfr_div(temporary, temporary, z, MPFR_RNDN);
    mpfr_log(phase, temporary, MPFR_RNDN);
    mpfr_sub(phase, phase, w, MPFR_RNDN);
  } else if (x > n) {
    mpfr_ui_div(temporary, 1, z, MPFR_RNDN);
    mpfr_acos(temporary, temporary, MPFR_RNDN);
    mpfr_sub(phase, w, temporary, MPFR_RNDN);
  } else {
    mpfr_set_zero(phase, 1);
  }
  mpfr_mul_d(zeta, phase, 1.5, MPFR_RNDN);
  mpfr_cbrt(zeta, zeta, MPFR_RNDN);
  mpfr_sqr(zeta, zeta, MPFR_RNDN);
  if (x > n)
    mpfr_neg(zeta, zeta, MPFR_RNDN);
  if (x == n) {
    mpfr_set_ui(amplitude, 2, MPFR_RNDN);
    mpfr_cbrt(amplitude, amplitude, MPFR_RNDN);
  } else {
    mpfr_div(amplitude, zeta, square, MPFR_RNDN);
    mpfr_mul_ui(amplitude, amplitude, 4, MPFR_RNDN);
    mpfr_sqrt(amplitude, amplitude, MPFR_RNDN);
    mpfr_sqrt(amplitude, amplitude, MPFR_RNDN);
  }
  result[0] = mpfr_get_d(zeta, MPFR_RNDN);
  result[1] = mpfr_get_d(amplitude, MPFR_RNDN);
  result[2] = mpfr_get_d(phase, MPFR_RNDN);
  mpfr_clears(z, square, w, phase, zeta, amplitude, temporary, (mpfr_ptr)0);
}
} // namespace

TEST(LlvmLibcBesselTransitionTest, IndependentHighPrecisionVariables) {
  LIBC_NAMESPACE::fputil::testing::ForceRoundingMode guard(
      LIBC_NAMESPACE::fputil::testing::RoundingMode::Nearest);
  ASSERT_TRUE(guard.success);
  const double orders[] = {1,     2,      499,          500,
                           10000, 0x1p20, 2147483647.0, 2147483648.0};
  auto check = [&](double n, double x) {
    double low[3], high[3];
    reference(n, x, 256, low);
    reference(n, x, 512, high);
    auto result = LIBC_NAMESPACE::math::bessel_transition(n, x);
    const double actual[] = {result.zeta, result.amplitude, result.phase};
    for (unsigned i = 0; i != 3; ++i) {
      ASSERT_EQ(Bits(low[i]).uintval(), Bits(high[i]).uintval());
      double error = actual[i] - high[i];
      double scale = high[i] < 0 ? -high[i] : high[i];
      EXPECT_TRUE(error <= 0x1p-48 * scale && error >= -0x1p-48 * scale);
    }
  };
  for (double n : orders) {
    for (int displacement = -4; displacement <= 4; ++displacement)
      check(n, Bits(Bits(n).uintval() + displacement).get_val());
    for (int k = -128; k <= 120; ++k)
      check(n, n * (1.0 + double(k) / 1024));
  }
}
