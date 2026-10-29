//===-- Tests for integer Bessel composition ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/math/bessel_integer.h"
#include "src/math/jn.h"
#include "src/math/yn.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"
#include <mpfr.h>

namespace {
using Bits = LIBC_NAMESPACE::fputil::FPBits<double>;
using LIBC_NAMESPACE::fputil::testing::ForceRoundingMode;
using LIBC_NAMESPACE::fputil::testing::RoundingMode;
using LIBC_NAMESPACE::math::bessel_integer_result;
using LIBC_NAMESPACE::math::BesselFloat;
const RoundingMode MODES[] = {RoundingMode::Nearest, RoundingMode::Downward,
                              RoundingMode::Upward, RoundingMode::TowardZero};
void set_wide(mpfr_t result, BesselFloat value) {
  mpfr_set_ui(result, 0, MPFR_RNDN);
  for (int shift = 96; shift >= 0; shift -= 32) {
    mpfr_mul_2ui(result, result, 32, MPFR_RNDN);
    mpfr_add_ui(result, result, uint32_t(value.mantissa >> shift), MPFR_RNDN);
  }
  mpfr_mul_2si(result, result, value.exponent, MPFR_RNDN);
  if (value.sign.is_neg())
    mpfr_neg(result, result, MPFR_RNDN);
}
} // namespace

TEST(LlvmLibcBesselIntegerTest, ExponentialScale) {
  ForceRoundingMode guard(RoundingMode::Nearest);
  ASSERT_TRUE(guard.success);
  mpfr_t input, reference, actual, error, limit;
  mpfr_inits2(512, input, reference, actual, error, limit, (mpfr_ptr)0);
  const unsigned orders[] = {2049U, 4096U};
  for (unsigned n : orders) {
    const double arguments[] = {0.5, 0.95 * n};
    for (double x : arguments) {
      auto pair = LIBC_NAMESPACE::math::bessel_integer(n, x);
      for (unsigned kind = 0; kind < 2; ++kind) {
        mpfr_set_d(input, x, MPFR_RNDN);
        (kind ? mpfr_yn : mpfr_jn)(reference, n, input, MPFR_RNDN);
        set_wide(actual, kind ? pair.y : pair.j);
        mpfr_mul_2si(actual, actual, kind ? pair.y_scale : pair.j_scale,
                     MPFR_RNDN);
        mpfr_sub(error, actual, reference, MPFR_RNDN);
        mpfr_abs(error, error, MPFR_RNDN);
        mpfr_abs(limit, reference, MPFR_RNDN);
        mpfr_mul_2si(limit, limit, -40, MPFR_RNDN);
        EXPECT_TRUE(mpfr_number_p(error) && mpfr_cmp(error, limit) <= 0);
      }
    }
  }
  auto extreme = LIBC_NAMESPACE::math::bessel_integer(
      0x80000000U, Bits::min_subnormal().get_val());
  EXPECT_TRUE(extreme.j_scale < -0x7fffffffLL);
  EXPECT_TRUE(extreme.y_scale > 0x7fffffffLL);
  EXPECT_FALSE(extreme.j.mantissa.is_zero());
  EXPECT_FALSE(extreme.y.mantissa.is_zero());
  mpfr_clears(input, reference, actual, error, limit, (mpfr_ptr)0);
}

TEST(LlvmLibcBesselIntegerTest, SignedDirectedRangeConversion) {
  for (auto mode : MODES) {
    ForceRoundingMode guard(mode);
    ASSERT_TRUE(guard.success);
    for (unsigned sign = 0; sign < 2; ++sign) {
      bool negative = sign != 0;
      LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
      LIBC_NAMESPACE::fputil::raise_except(FE_DIVBYZERO);
      libc_errno = 0;
      double result =
          bessel_integer_result(BesselFloat(1.0), 0x100000000LL, negative);
      bool infinite = mode == RoundingMode::Nearest ||
                      (mode == RoundingMode::Upward && !negative) ||
                      (mode == RoundingMode::Downward && negative);
      EXPECT_EQ(Bits(result).is_inf(), infinite);
      EXPECT_EQ(Bits(result).is_neg(), negative);
      EXPECT_EQ(int(libc_errno), ERANGE);
      EXPECT_EQ(LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT),
                FE_DIVBYZERO | FE_OVERFLOW | FE_INEXACT);
      LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
      libc_errno = 0;
      result =
          bessel_integer_result(BesselFloat(1.0), -0x100000000LL, negative);
      bool away = (mode == RoundingMode::Upward && !negative) ||
                  (mode == RoundingMode::Downward && negative);
      EXPECT_EQ(Bits(result).is_zero(), !away);
      EXPECT_EQ(Bits(result).is_neg(), negative);
      EXPECT_EQ(int(libc_errno), ERANGE);
      EXPECT_EQ(LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT),
                FE_UNDERFLOW | FE_INEXACT);
    }
  }
}

TEST(LlvmLibcBesselIntegerTest, EntrySpecialValuesAndEnvironment) {
  const int orders[] = {
      0, 1, -1, 2, -3, 2049, -2049, 2147483647, (-2147483647 - 1)};
  for (auto mode : MODES) {
    ForceRoundingMode guard(mode);
    ASSERT_TRUE(guard.success);
    for (int n : orders) {
      LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
      libc_errno = 17;
      double quiet = Bits::quiet_nan().get_val();
      EXPECT_TRUE(Bits(LIBC_NAMESPACE::jn(n, quiet)).is_nan());
      EXPECT_TRUE(Bits(LIBC_NAMESPACE::yn(n, quiet)).is_nan());
      EXPECT_EQ(int(libc_errno), 17);
      EXPECT_EQ(LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT), 0);
      double signaling = Bits::signaling_nan().get_val();
      EXPECT_TRUE(Bits(LIBC_NAMESPACE::jn(n, signaling)).is_quiet_nan());
      EXPECT_EQ(LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT), FE_INVALID);
      LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
      EXPECT_TRUE(Bits(LIBC_NAMESPACE::yn(n, signaling)).is_quiet_nan());
      EXPECT_EQ(LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT), FE_INVALID);
      for (unsigned s = 0; s < 2; ++s) {
        bool negative = s != 0;
        auto sign =
            negative ? LIBC_NAMESPACE::Sign::NEG : LIBC_NAMESPACE::Sign::POS;
        double zero = Bits::zero(sign).get_val();
        double result = LIBC_NAMESPACE::jn(n, zero);
        if (n == 0) {
          EXPECT_TRUE(result == 1.0);
        } else {
          EXPECT_TRUE(Bits(result).is_zero());
          EXPECT_EQ(Bits(result).is_neg(),
                    bool((n & 1) && ((n < 0) != negative)));
        }
        LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
        libc_errno = 0;
        result = LIBC_NAMESPACE::yn(n, zero);
        EXPECT_TRUE(Bits(result).is_inf());
        EXPECT_EQ(Bits(result).is_neg(), !bool((n & 1) && n < 0));
        EXPECT_EQ(int(libc_errno), ERANGE);
        EXPECT_EQ(LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT),
                  FE_DIVBYZERO);
      }
      LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
      libc_errno = 0;
      EXPECT_EQ(Bits(LIBC_NAMESPACE::yn(n, -1.0)).uintval(),
                Bits::inf((n < 0 && (n & 1)) ? LIBC_NAMESPACE::Sign::POS
                                           : LIBC_NAMESPACE::Sign::NEG)
                    .uintval());
      EXPECT_EQ(int(libc_errno), EDOM);
      EXPECT_EQ(LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT), FE_INVALID);
      LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
      LIBC_NAMESPACE::fputil::raise_except(FE_DIVBYZERO);
      libc_errno = 17;
      double x = n < -1 || n > 1 ? (n < 0 ? -double(n) : double(n)) : 1.0;
      EXPECT_TRUE(Bits(LIBC_NAMESPACE::jn(n, x)).is_finite());
      EXPECT_TRUE(Bits(LIBC_NAMESPACE::yn(n, x)).is_finite());
      EXPECT_EQ(int(libc_errno), 17);
      EXPECT_EQ(LIBC_NAMESPACE::fputil::get_round(), guard.rounding_mode);
      EXPECT_EQ(LIBC_NAMESPACE::fputil::test_except(FE_INVALID | FE_DIVBYZERO |
                                                    FE_OVERFLOW | FE_UNDERFLOW),
                FE_DIVBYZERO);
    }
  }
}

TEST(LlvmLibcBesselIntegerTest, EntryFourModeFiniteReference) {
  mpfr_t input, reference, actual, error, limit, amplitude, temporary;
  mpfr_inits2(512, input, reference, actual, error, limit, amplitude, temporary,
              (mpfr_ptr)0);
  const int orders[] = {2, 17, 2049};
  for (int n : orders) {
    const double arguments[] = {0.5, double(n), double(n) * 1.001};
    for (double x : arguments) {
      mpfr_set_d(input, x, MPFR_RNDN);
      for (unsigned kind = 0; kind < 2; ++kind) {
        (kind ? mpfr_yn : mpfr_jn)(reference, n, input, MPFR_RNDN);
        mpfr_abs(limit, reference, MPFR_RNDN);
        if (x >= n) {
          mpfr_set_si(amplitude, n, MPFR_RNDN);
          mpfr_cbrt(amplitude, amplitude, MPFR_RNDN);
          mpfr_ui_div(amplitude, 1, amplitude, MPFR_RNDN);
          if (x > n) {
            mpfr_sqr(temporary, input, MPFR_RNDN);
            mpfr_sub_ui(temporary, temporary, unsigned(n) * unsigned(n),
                        MPFR_RNDN);
            mpfr_sqrt(temporary, temporary, MPFR_RNDN);
            mpfr_sqrt(temporary, temporary, MPFR_RNDN);
            mpfr_ui_div(temporary, 1, temporary, MPFR_RNDN);
            mpfr_min(amplitude, amplitude, temporary, MPFR_RNDN);
          }
          mpfr_max(limit, limit, amplitude, MPFR_RNDN);
        }
        mpfr_mul_2si(limit, limit, -40, MPFR_RNDN);
        mpfr_set_ui_2exp(temporary, 1, -1074, MPFR_RNDN);
        mpfr_max(limit, limit, temporary, MPFR_RNDN);
        for (auto mode : MODES) {
          ForceRoundingMode guard(mode);
          ASSERT_TRUE(guard.success);
          for (unsigned parity = 0; parity < 2; ++parity) {
            double result = (kind ? LIBC_NAMESPACE::yn
                                  : LIBC_NAMESPACE::jn)(parity ? -n : n, x);
            // These Yn samples can exceed binary64; conversion correctness
            // has a separate exact four-mode test, not an infinity tolerance.
            if (kind && n == 2049 && x == 0.5) {
              EXPECT_TRUE(Bits(result).is_inf() ||
                          Bits(result).abs().uintval() ==
                              Bits::max_normal().uintval());
              EXPECT_EQ(Bits(result).is_neg(), parity == 0);
              continue;
            }
            mpfr_set_d(actual, result, MPFR_RNDN);
            if (parity && (n & 1))
              mpfr_neg(actual, actual, MPFR_RNDN);
            mpfr_sub(error, actual, reference, MPFR_RNDN);
            mpfr_abs(error, error, MPFR_RNDN);
            EXPECT_TRUE(mpfr_number_p(error) && mpfr_cmp(error, limit) <= 0);
          }
        }
      }
    }
  }
  mpfr_clears(input, reference, actual, error, limit, amplitude, temporary,
              (mpfr_ptr)0);
}
