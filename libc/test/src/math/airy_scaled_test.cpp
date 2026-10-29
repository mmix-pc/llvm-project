//===-- Tests for internal Airy kernels -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/FPUtil/FEnvImpl.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/libc_errno.h"
#include "src/__support/math/airy_scaled.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"
#include <mpfr.h>

namespace {
using LIBC_NAMESPACE::math::ScaledAiry;
using Bits = LIBC_NAMESPACE::fputil::FPBits<double>;

// Independent entire solutions of y''=xy, with gamma-derived initial data.
// High precision is necessary: Ai is the difference of two growing series.
bool reference(double x, mpfr_prec_t precision, double (&result)[4]) {
  mpfr_t a, cube, f, g, fp, gp, ft, gt, term, c1, c2, third, scale, root;
  mpfr_inits2(precision, a, cube, f, g, fp, gp, ft, gt, term, c1, c2, third,
              scale, root, (mpfr_ptr)0);
  mpfr_set_d(a, x, MPFR_RNDN);
  mpfr_pow_ui(cube, a, 3, MPFR_RNDN);
  mpfr_set_ui(f, 1, MPFR_RNDN);
  mpfr_set(g, a, MPFR_RNDN);
  mpfr_set_zero(fp, 1);
  mpfr_set_ui(gp, 1, MPFR_RNDN);
  mpfr_set(ft, f, MPFR_RNDN);
  mpfr_set(gt, g, MPFR_RNDN);
  bool converged = x == 0;
  for (unsigned k = 1; x != 0 && k < 10000; ++k) {
    unsigned m = 3 * k;
    mpfr_mul(ft, ft, cube, MPFR_RNDN);
    mpfr_div_ui(ft, ft, m * (m - 1), MPFR_RNDN);
    mpfr_mul(gt, gt, cube, MPFR_RNDN);
    mpfr_div_ui(gt, gt, m * (m + 1), MPFR_RNDN);
    mpfr_add(f, f, ft, MPFR_RNDN);
    mpfr_add(g, g, gt, MPFR_RNDN);
    mpfr_mul_ui(term, ft, m, MPFR_RNDN);
    mpfr_div(term, term, a, MPFR_RNDN);
    mpfr_add(fp, fp, term, MPFR_RNDN);
    mpfr_mul_ui(term, gt, m + 1, MPFR_RNDN);
    mpfr_div(term, term, a, MPFR_RNDN);
    mpfr_add(gp, gp, term, MPFR_RNDN);
    if (mpfr_get_exp(ft) < -precision && mpfr_get_exp(gt) < -precision &&
        mpfr_get_exp(term) < -precision) {
      converged = true;
      break;
    }
  }
  mpfr_set_ui(third, 1, MPFR_RNDN);
  mpfr_div_ui(third, third, 3, MPFR_RNDN);
  mpfr_gamma(c2, third, MPFR_RNDN);
  mpfr_set_ui(root, 3, MPFR_RNDN);
  mpfr_cbrt(root, root, MPFR_RNDN);
  mpfr_mul(c2, c2, root, MPFR_RNDN);
  mpfr_ui_div(c2, 1, c2, MPFR_RNDN);
  mpfr_mul_ui(third, third, 2, MPFR_RNDN);
  mpfr_gamma(c1, third, MPFR_RNDN);
  mpfr_mul(c1, c1, root, MPFR_RNDN);
  mpfr_mul(c1, c1, root, MPFR_RNDN);
  mpfr_ui_div(c1, 1, c1, MPFR_RNDN);
  mpfr_mul(f, f, c1, MPFR_RNDN);
  mpfr_mul(g, g, c2, MPFR_RNDN);
  mpfr_mul(fp, fp, c1, MPFR_RNDN);
  mpfr_mul(gp, gp, c2, MPFR_RNDN);
  if (x >= 0) {
    mpfr_sqrt(root, a, MPFR_RNDN);
    mpfr_mul(scale, a, root, MPFR_RNDN);
    mpfr_mul_ui(scale, scale, 2, MPFR_RNDN);
    mpfr_div_ui(scale, scale, 3, MPFR_RNDN);
    mpfr_exp(scale, scale, MPFR_RNDN);
  } else {
    mpfr_set_ui(scale, 1, MPFR_RNDN);
  }
  mpfr_sub(term, f, g, MPFR_RNDN);
  mpfr_mul(term, term, scale, MPFR_RNDN);
  result[0] = mpfr_get_d(term, MPFR_RNDN);
  mpfr_sub(term, fp, gp, MPFR_RNDN);
  mpfr_mul(term, term, scale, MPFR_RNDN);
  result[1] = mpfr_get_d(term, MPFR_RNDN);
  mpfr_set_ui(root, 3, MPFR_RNDN);
  mpfr_sqrt(root, root, MPFR_RNDN);
  mpfr_add(term, f, g, MPFR_RNDN);
  mpfr_mul(term, term, root, MPFR_RNDN);
  mpfr_div(term, term, scale, MPFR_RNDN);
  result[2] = mpfr_get_d(term, MPFR_RNDN);
  mpfr_add(term, fp, gp, MPFR_RNDN);
  mpfr_mul(term, term, root, MPFR_RNDN);
  mpfr_div(term, term, scale, MPFR_RNDN);
  result[3] = mpfr_get_d(term, MPFR_RNDN);
  mpfr_clears(a, cube, f, g, fp, gp, ft, gt, term, c1, c2, third, scale, root,
              (mpfr_ptr)0);
  return converged;
}

// DLMF 9.7.2 and 9.7.5--8, independent of the kernel's rational fits.
// For x >= 128, 33 terms give a remainder below 2^-200 before the
// algebraic prefactor, including the Bi chi(n + sigma) + 1 multiplier.
bool asymptotic_reference(double x, mpfr_prec_t precision,
                          double (&result)[4]) {
  mpfr_t zeta, root, factor, u, v, tail, sum[4];
  mpfr_inits2(precision, zeta, root, factor, u, v, tail, (mpfr_ptr)0);
  mpfr_set_d(root, x, MPFR_RNDN);
  mpfr_sqrt(root, root, MPFR_RNDN);
  mpfr_mul_d(zeta, root, x, MPFR_RNDN);
  mpfr_mul_ui(zeta, zeta, 2, MPFR_RNDN);
  mpfr_div_ui(zeta, zeta, 3, MPFR_RNDN);
  mpfr_sqrt(root, root, MPFR_RNDN);
  for (auto &s : sum) {
    mpfr_init2(s, precision);
    mpfr_set_ui(s, 1, MPFR_RNDN);
  }
  mpfr_set_ui(u, 1, MPFR_RNDN);
  for (unsigned k = 1; k <= 33; ++k) {
    mpfr_mul_ui(u, u, (6 * k - 5) * (6 * k - 1), MPFR_RNDN);
    mpfr_div_ui(u, u, 72 * k, MPFR_RNDN);
    mpfr_div(u, u, zeta, MPFR_RNDN);
    mpfr_mul_ui(v, u, 6 * k + 1, MPFR_RNDN);
    mpfr_div_si(v, v, 1 - long(6 * k), MPFR_RNDN);
    if (k == 33)
      break;
    mpfr_add(sum[2], sum[2], u, MPFR_RNDN);
    mpfr_add(sum[3], sum[3], v, MPFR_RNDN);
    if (k & 1) {
      mpfr_sub(sum[0], sum[0], u, MPFR_RNDN);
      mpfr_sub(sum[1], sum[1], v, MPFR_RNDN);
    } else {
      mpfr_add(sum[0], sum[0], u, MPFR_RNDN);
      mpfr_add(sum[1], sum[1], v, MPFR_RNDN);
    }
  }
  // A conservative factor 16 exceeds both chi(33 + sigma) + 1 factors.
  mpfr_abs(tail, v, MPFR_RNDN);
  mpfr_mul_ui(tail, tail, 16, MPFR_RNDN);
  bool bounded = mpfr_get_exp(tail) < -200;
  mpfr_const_pi(factor, MPFR_RNDN);
  mpfr_sqrt(factor, factor, MPFR_RNDN);
  mpfr_ui_div(factor, 1, factor, MPFR_RNDN);
  for (unsigned i = 0; i != 4; ++i) {
    mpfr_mul(sum[i], sum[i], factor, MPFR_RNDN);
    if (i & 1)
      mpfr_mul(sum[i], sum[i], root, MPFR_RNDN);
    else
      mpfr_div(sum[i], sum[i], root, MPFR_RNDN);
    if (i < 2)
      mpfr_div_si(sum[i], sum[i], i == 0 ? 2 : -2, MPFR_RNDN);
    result[i] = mpfr_get_d(sum[i], MPFR_RNDN);
    mpfr_clear(sum[i]);
  }
  mpfr_clears(zeta, root, factor, u, v, tail, (mpfr_ptr)0);
  return bounded;
}
// Negative-axis expansion from DLMF 9.7.9--12, at |x| >= 128.
bool oscillatory_reference(double a, mpfr_prec_t precision,
                           double (&result)[4]) {
  mpfr_t zeta, root, u, v, factor, phase, s, c, t, tail, sum[4];
  mpfr_inits2(precision, zeta, root, u, v, factor, phase, s, c, t, tail,
              (mpfr_ptr)0);
  mpfr_set_d(root, a, MPFR_RNDN);
  mpfr_sqrt(root, root, MPFR_RNDN);
  mpfr_mul_d(zeta, root, a, MPFR_RNDN);
  mpfr_mul_ui(zeta, zeta, 2, MPFR_RNDN);
  mpfr_div_ui(zeta, zeta, 3, MPFR_RNDN);
  mpfr_sqrt(root, root, MPFR_RNDN);
  mpfr_const_pi(phase, MPFR_RNDN);
  mpfr_div_ui(phase, phase, 4, MPFR_RNDN);
  mpfr_add(phase, phase, zeta, MPFR_RNDN);
  mpfr_sin_cos(s, c, phase, MPFR_RNDN);
  for (unsigned i = 0; i < 4; ++i) {
    mpfr_init2(sum[i], precision);
    mpfr_set_ui(sum[i], (i & 1) ? 0 : 1, MPFR_RNDN);
  }
  mpfr_set_ui(u, 1, MPFR_RNDN);
  for (unsigned k = 1; k <= 34; ++k) {
    mpfr_mul_ui(u, u, (6 * k - 5) * (6 * k - 1), MPFR_RNDN);
    mpfr_div_ui(u, u, 72 * k, MPFR_RNDN);
    mpfr_div(u, u, zeta, MPFR_RNDN);
    mpfr_mul_ui(v, u, 6 * k + 1, MPFR_RNDN);
    mpfr_div_si(v, v, 1 - long(6 * k), MPFR_RNDN);
    if (k >= 33) {
      mpfr_abs(tail, v, MPFR_RNDN);
      if (mpfr_get_exp(tail) >= -200) {
        for (auto &value : sum)
          mpfr_clear(value);
        mpfr_clears(zeta, root, u, v, factor, phase, s, c, t, tail,
                    (mpfr_ptr)0);
        return false;
      }
      continue;
    }
    unsigned index = k & 1;
    if ((k / 2) & 1) {
      mpfr_sub(sum[index], sum[index], u, MPFR_RNDN);
      mpfr_sub(sum[index + 2], sum[index + 2], v, MPFR_RNDN);
    } else {
      mpfr_add(sum[index], sum[index], u, MPFR_RNDN);
      mpfr_add(sum[index + 2], sum[index + 2], v, MPFR_RNDN);
    }
  }
  mpfr_const_pi(factor, MPFR_RNDN);
  mpfr_sqrt(factor, factor, MPFR_RNDN);
  mpfr_ui_div(factor, 1, factor, MPFR_RNDN);
  for (unsigned i = 0; i < 4; ++i) {
    unsigned index = (i & 1) ? 2 : 0;
    bool sin_first = i == 0 || i == 3;
    mpfr_mul(phase, sum[index], sin_first ? s : c, MPFR_RNDN);
    mpfr_mul(t, sum[index + 1], sin_first ? c : s, MPFR_RNDN);
    if (sin_first)
      mpfr_sub(phase, phase, t, MPFR_RNDN);
    else
      mpfr_add(phase, phase, t, MPFR_RNDN);
    mpfr_mul(phase, phase, factor, MPFR_RNDN);
    if (i & 1)
      mpfr_mul(phase, phase, root, MPFR_RNDN);
    else
      mpfr_div(phase, phase, root, MPFR_RNDN);
    if (i == 1)
      mpfr_neg(phase, phase, MPFR_RNDN);
    result[i] = mpfr_get_d(phase, MPFR_RNDN);
  }
  for (auto &value : sum)
    mpfr_clear(value);
  mpfr_clears(zeta, root, u, v, factor, phase, s, c, t, tail, (mpfr_ptr)0);
  return true;
}
} // namespace

TEST(LlvmLibcScaledAiryTest, IndependentEntireSeries) {
  LIBC_NAMESPACE::fputil::testing::ForceRoundingMode guard(
      LIBC_NAMESPACE::fputil::testing::RoundingMode::Nearest);
  ASSERT_TRUE(guard.success);
  auto check = [&](double x) {
    double low[4], high[4];
    ASSERT_TRUE(reference(x, 2048, low));
    ASSERT_TRUE(reference(x, 4096, high));
    ScaledAiry values = LIBC_NAMESPACE::math::airy_scaled_positive(x);
    const double actual[] = {values.ai, values.ai_prime, values.bi,
                             values.bi_prime};
    for (unsigned i = 0; i != 4; ++i) {
      ASSERT_EQ(Bits(low[i]).uintval(), Bits(high[i]).uintval());
      // A local prerequisite bound, not the public integer-order Bessel bound.
      double error = actual[i] - high[i];
      double magnitude = high[i] < 0 ? -high[i] : high[i];
      EXPECT_TRUE(error <= 0x1p-45 * magnitude &&
                  error >= -0x1p-45 * magnitude);
    }
  };
  for (unsigned i = 0; i <= 64; ++i)
    check(double(i) / 8);
  const double edges[] = {0x1p-54, 2.09, 8.3203353, 16, 32, 64, 100};
  for (double x : edges) {
    uint64_t bits = Bits(x).uintval();
    check(Bits(bits - 1).get_val());
    check(x);
    check(Bits(bits + 1).get_val());
  }
}

TEST(LlvmLibcScaledAiryTest, LargeArgumentsDoNotEvaluateExponentials) {
  namespace fp = LIBC_NAMESPACE::fputil;
  fp::testing::ForceRoundingMode guard(fp::testing::RoundingMode::Nearest);
  ASSERT_TRUE(guard.success);
  for (int exponent = 7; exponent <= 32; ++exponent) {
    double x = double(uint64_t(1) << exponent);
    libc_errno = 0;
    fp::clear_except(FE_ALL_EXCEPT);
    ScaledAiry values = LIBC_NAMESPACE::math::airy_scaled_positive(x);
    EXPECT_TRUE(values.ai > 0 && values.ai_prime < 0 && values.bi > 0 &&
                values.bi_prime > 0);
    EXPECT_EQ(
        fp::test_except(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW),
        0);
    EXPECT_EQ(int(libc_errno), 0);
    // Supplemental identity only; the independent oracle is the previous test.
    double wronskian =
        values.ai * values.bi_prime - values.ai_prime * values.bi;
    EXPECT_TRUE(wronskian > 0.31830988618378 && wronskian < 0.31830988618380);
    double low[4], high[4];
    ASSERT_TRUE(asymptotic_reference(x, 256, low));
    ASSERT_TRUE(asymptotic_reference(x, 512, high));
    const double actual[] = {values.ai, values.ai_prime, values.bi,
                             values.bi_prime};
    for (unsigned i = 0; i != 4; ++i) {
      ASSERT_EQ(Bits(low[i]).uintval(), Bits(high[i]).uintval());
      double error = actual[i] - high[i];
      double magnitude = high[i] < 0 ? -high[i] : high[i];
      EXPECT_TRUE(error <= 0x1p-45 * magnitude &&
                  error >= -0x1p-45 * magnitude);
    }
  }
}

TEST(LlvmLibcScaledAiryTest, NegativeEntireSeries) {
  LIBC_NAMESPACE::fputil::testing::ForceRoundingMode guard(
      LIBC_NAMESPACE::fputil::testing::RoundingMode::Nearest);
  ASSERT_TRUE(guard.success);
  auto check = [&](double x) {
    double low[4], high[4];
    ASSERT_TRUE(reference(x, 2048, low));
    ASSERT_TRUE(reference(x, 4096, high));
    ScaledAiry result = LIBC_NAMESPACE::math::airy_negative(x);
    const double actual[] = {result.ai, result.ai_prime, result.bi,
                             result.bi_prime};
    double fourth_root =
        x < -1 ? LIBC_NAMESPACE::math::sqrt(LIBC_NAMESPACE::math::sqrt(-x)) : 1;
    for (unsigned i = 0; i < 4; ++i) {
      ASSERT_EQ(Bits(low[i]).uintval(), Bits(high[i]).uintval());
      double scale = (i & 1) ? fourth_root : 1 / fourth_root;
      double error = actual[i] - high[i];
      EXPECT_TRUE(error < 0x1p-45 * scale && error > -0x1p-45 * scale);
    }
  };
  for (unsigned i = 0; i <= 160; ++i)
    check(-double(i) / 16);
  const double edges[] = {0x1p-54,
                          2.09,
                          2.338107410459767,
                          1.173713222709128,
                          1.018792971647471,
                          2.294439682614123,
                          32,
                          64,
                          100};
  for (double x : edges) {
    uint64_t bits = Bits(x).uintval();
    check(-Bits(bits - 1).get_val());
    check(-x);
    check(-Bits(bits + 1).get_val());
  }
}

TEST(LlvmLibcScaledAiryTest, NegativeLargePhase) {
  LIBC_NAMESPACE::fputil::testing::ForceRoundingMode guard(
      LIBC_NAMESPACE::fputil::testing::RoundingMode::Nearest);
  ASSERT_TRUE(guard.success);
  for (unsigned exponent = 7; exponent <= 32; ++exponent) {
    double a = double(uint64_t(1) << exponent);
    for (int offset = -1; offset <= 1; ++offset) {
      double x = Bits(Bits(a).uintval() + offset).get_val();
      if (x < 128 || x > 0x1p32)
        continue;
      double low[4], high[4];
      ASSERT_TRUE(oscillatory_reference(x, 256, low));
      ASSERT_TRUE(oscillatory_reference(x, 512, high));
      ScaledAiry result = LIBC_NAMESPACE::math::airy_negative(-x);
      const double actual[] = {result.ai, result.ai_prime, result.bi,
                               result.bi_prime};
      double fourth_root =
          LIBC_NAMESPACE::math::sqrt(LIBC_NAMESPACE::math::sqrt(x));
      for (unsigned i = 0; i < 4; ++i) {
        ASSERT_EQ(Bits(low[i]).uintval(), Bits(high[i]).uintval());
        double scale = (i & 1) ? fourth_root : 1 / fourth_root;
        double error = actual[i] - high[i];
        EXPECT_TRUE(error < 0x1p-45 * scale && error > -0x1p-45 * scale);
      }
    }
  }
}
