//===-- CPU gamma accuracy regression tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/FPUtil/FPBits.h"
#include "src/math/lgamma.h"
#include "src/math/lgamma_r.h"
#include "src/math/tgamma.h"
#include "src/math/tgammaf.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"
#include "utils/MPFRWrapper/mpfr_inc.h"

using LIBC_NAMESPACE::fputil::testing::ForceRoundingMode;
using LIBC_NAMESPACE::fputil::testing::RoundingMode;

// These are regression budgets, not proven error bounds or correct-rounding
// claims. In particular, cancellation at negative lgamma zeros needs an
// absolute budget. Use MPFR directly because the shared matcher has no Gamma
// operation.
class LlvmLibcGammaAccuracyTest : public LIBC_NAMESPACE::testing::Test {
public:
  void check(double x, bool logarithmic, bool single) {
    const RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::Upward,
                                  RoundingMode::Downward,
                                  RoundingMode::TowardZero};
    mpfr_t input, reference, error, budget;
    mpfr_inits2(256, input, reference, error, budget, (mpfr_ptr)0);
    mpfr_set_d(input, x, MPFR_RNDN);
    int reference_sign = 1;
    if (logarithmic) {
      mpfr_lgamma(reference, &reference_sign, input, MPFR_RNDN);
    } else {
      mpfr_gamma(reference, input, MPFR_RNDN);
    }
    mpfr_abs(budget, reference, MPFR_RNDN);
    if (logarithmic && mpfr_cmp_d(budget, 1.0) < 0)
      mpfr_set_d(budget, 1.0, MPFR_RNDN);
    mpfr_mul_d(budget, budget, single ? 0x1p-22 : 0x1p-40, MPFR_RNDN);
    mpfr_add_d(budget, budget, single ? 0x1p-148 : 0x1p-1073, MPFR_RNDN);
    for (RoundingMode mode : modes) {
      ForceRoundingMode rounding(mode);
      if (!rounding.success)
        continue;
      double actual = logarithmic ? LIBC_NAMESPACE::lgamma(x)
                      : single    ? static_cast<double>(LIBC_NAMESPACE::tgammaf(
                                        static_cast<float>(x)))
                                  : LIBC_NAMESPACE::tgamma(x);
      bool invalid =
          LIBC_NAMESPACE::fputil::FPBits<double>(actual).is_inf_or_nan();
      EXPECT_FALSE(invalid);
      if (invalid)
        continue;
      if (logarithmic) {
        int actual_sign = 0;
        EXPECT_FP_EQ(actual, LIBC_NAMESPACE::lgamma_r(x, &actual_sign));
        EXPECT_EQ(reference_sign, actual_sign);
      }
      mpfr_sub_d(error, reference, actual, MPFR_RNDN);
      mpfr_abs(error, error, MPFR_RNDN);
      EXPECT_TRUE(mpfr_cmp(error, budget) <= 0);
    }
    mpfr_clears(input, reference, error, budget, (mpfr_ptr)0);
  }
};

TEST_F(LlvmLibcGammaAccuracyTest, DoubleRangeAndPoleNeighbors) {
  const double inputs[] = {0x1p-1022,
                           -0x1p-1022,
                           0x1p-100,
                           -0x1p-100,
                           0x1p-26,
                           -0x1p-26,
                           0.125,
                           -0.125,
                           0.5,
                           -0.5,
                           0x1.fffffffffffffp-1,
                           0x1.0000000000001p0,
                           -0x1.fffffffffffffp-1,
                           -0x1.0000000000001p0,
                           1.4616321449683623,
                           1.5,
                           -1.5,
                           2.5,
                           -2.5,
                           -0x1.fffffffffffffp0,
                           -0x1.0000000000001p1,
                           7.5,
                           8.0,
                           8.5,
                           36.5,
                           100.5,
                           170.5,
                           171.5,
                           -40.5,
                           -170.5,
                           -180.5,
                           -183.5,
                           -184.5};
  for (double x : inputs) {
    check(x, false, false);
    check(x, true, false);
  }
  for (int i = -511; i < 686; ++i) {
    double x = static_cast<double>(i) * 0.25 + 0.125;
    check(x, false, false);
    check(x, true, false);
  }
}

TEST_F(LlvmLibcGammaAccuracyTest, FloatRangeAndPoleNeighbors) {
  const float inputs[] = {
      0x1p-126f,       -0x1p-126f,     0x1p-26f,       -0x1p-26f,      0.5f,
      -0.5f,           1.5f,           -1.5f,          2.5f,           -2.5f,
      -0x1.fffffep-1f, -0x1.000002p0f, -0x1.fffffep0f, -0x1.000002p1f, 8.5f,
      34.5f,           35.0f,          -33.5f,         -38.5f,         -40.5f,
      -41.5f};
  for (float x : inputs)
    check(static_cast<double>(x), false, true);
}

TEST_F(LlvmLibcGammaAccuracyTest, LargeLogGammaAndNegativeZeros) {
  const double inputs[] = {0x1p58,
                           0x1p100,
                           0x1p1000,
                           -0x1.fffffffffffffp51,
                           -2.4570247382208006,
                           -2.7476826467274127,
                           -3.14358088834998,
                           -3.955294284858598};
  for (double x : inputs)
    check(x, true, false);
}

TEST_F(LlvmLibcGammaAccuracyTest, NegativeLogGammaRounding) {
  const RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::Upward,
                               RoundingMode::Downward,
                               RoundingMode::TowardZero};
  const mpfr_rnd_t mpfr_modes[] = {MPFR_RNDN, MPFR_RNDU, MPFR_RNDD, MPFR_RNDZ};
  mpfr_t input, reference;
  mpfr_inits2(256, input, reference, (mpfr_ptr)0);
  auto check_rounded = [&](double x) {
    mpfr_set_d(input, x, MPFR_RNDN);
    int sign;
    mpfr_lgamma(reference, &sign, input, MPFR_RNDN);
    for (unsigned i = 0; i < 4; ++i) {
      ForceRoundingMode rounding(modes[i]);
      if (!rounding.success)
        continue;
      double expected = mpfr_get_d(reference, mpfr_modes[i]);
      EXPECT_FP_EQ(expected, LIBC_NAMESPACE::lgamma(x));
    }
  };
  check_rounded(-2.5);
  for (int i = 0; i < 800; ++i)
    check_rounded(-0.125 - static_cast<double>(i) * 0.25);
  // Neighbors of roots exercise loss of relative precision in reflection.
  const double roots[] = {-2.4570247382208006, -2.7476826467274127,
                          -3.14358088834998, -3.955294284858598};
  for (double x : roots) {
    using Bits = LIBC_NAMESPACE::fputil::FPBits<double>;
    for (int offset = -2; offset <= 2; ++offset)
      check_rounded(Bits(Bits(x).uintval() + offset).get_val());
  }
  mpfr_clears(input, reference, (mpfr_ptr)0);
}
