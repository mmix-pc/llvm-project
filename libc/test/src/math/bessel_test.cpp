//===-- Tests for fixed-order Bessel functions ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/FPUtil/FEnvImpl.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/libc_errno.h"
#include "src/math/j0.h"
#include "src/math/j1.h"
#include "src/math/y0.h"
#include "src/math/y1.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"
#include <mpfr.h>

using Bits = LIBC_NAMESPACE::fputil::FPBits<double>;
using LIBC_NAMESPACE::Sign;
namespace fp = LIBC_NAMESPACE::fputil;

TEST(LlvmLibcBesselTest, SpecialValuesAndErrors) {
  constexpr double zeros[] = {0.0, -0.0};
  double (*second_kind[])(double) = {LIBC_NAMESPACE::y0, LIBC_NAMESPACE::y1};
  for (double zero : zeros) {
    EXPECT_TRUE(LIBC_NAMESPACE::j0(zero) == 1.0);
    EXPECT_EQ(Bits(LIBC_NAMESPACE::j1(zero)).uintval(), Bits(zero).uintval());
    for (auto function : second_kind) {
      libc_errno = 0;
      fp::clear_except(FE_ALL_EXCEPT);
      EXPECT_EQ(Bits(function(zero)).uintval(), Bits::inf(Sign::NEG).uintval());
      EXPECT_EQ(int(libc_errno), ERANGE);
      EXPECT_EQ(fp::test_except(FE_ALL_EXCEPT), FE_DIVBYZERO);
    }
  }
  double (*functions[])(double) = {LIBC_NAMESPACE::j0, LIBC_NAMESPACE::j1,
                                   LIBC_NAMESPACE::y0, LIBC_NAMESPACE::y1};
  for (auto function : functions) {
    libc_errno = 0;
    fp::clear_except(FE_ALL_EXCEPT);
    EXPECT_EQ(Bits(function(Bits::inf().get_val())).uintval(), uint64_t(0));
    EXPECT_EQ(int(libc_errno), 0);
    EXPECT_EQ(fp::test_except(FE_ALL_EXCEPT), 0);
    EXPECT_TRUE(Bits(function(Bits::quiet_nan().get_val())).is_nan());
    EXPECT_EQ(fp::test_except(FE_INVALID), 0);
    EXPECT_TRUE(Bits(function(Bits::signaling_nan().get_val())).is_nan());
    EXPECT_EQ(fp::test_except(FE_INVALID), FE_INVALID);
  }
  for (auto function : second_kind) {
    libc_errno = 0;
    fp::clear_except(FE_ALL_EXCEPT);
    EXPECT_TRUE(Bits(function(-1.0)).is_nan());
    EXPECT_EQ(int(libc_errno), EDOM);
    EXPECT_EQ(fp::test_except(FE_ALL_EXCEPT), FE_INVALID);
  }
  libc_errno = 0;
  fp::clear_except(FE_ALL_EXCEPT);
  EXPECT_TRUE(
      Bits(LIBC_NAMESPACE::y1(Bits::min_subnormal().get_val())).is_inf());
  EXPECT_EQ(int(libc_errno), ERANGE);
  EXPECT_EQ(fp::test_except(FE_OVERFLOW), FE_OVERFLOW);
  libc_errno = 0;
  fp::clear_except(FE_ALL_EXCEPT);
  EXPECT_TRUE(
      Bits(LIBC_NAMESPACE::j1(Bits::min_subnormal().get_val())).is_zero());
  EXPECT_EQ(int(libc_errno), ERANGE);
  EXPECT_EQ(fp::test_except(FE_UNDERFLOW), FE_UNDERFLOW);
}

TEST(LlvmLibcBesselTest, DirectedRangeErrors) {
  using fp::testing::RoundingMode;
  constexpr RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::Upward,
                                    RoundingMode::Downward,
                                    RoundingMode::TowardZero};
  for (auto mode : modes) {
    fp::testing::ForceRoundingMode guard(mode);
    ASSERT_TRUE(guard.success);
    // A pre-existing overflow flag must not hide this call's errno obligation.
    const int previous_flags[] = {0, FE_OVERFLOW};
    for (int previous : previous_flags) {
      libc_errno = 0;
      fp::clear_except(FE_ALL_EXCEPT);
      fp::raise_except(previous);
      double result = LIBC_NAMESPACE::y1(Bits::min_subnormal().get_val());
      bool finite =
          mode == RoundingMode::Upward || mode == RoundingMode::TowardZero;
      EXPECT_EQ(Bits(result).uintval(),
                (finite ? Bits::max_normal(Sign::NEG) : Bits::inf(Sign::NEG))
                    .uintval());
      EXPECT_EQ(int(libc_errno), ERANGE);
      EXPECT_EQ(fp::test_except(FE_ALL_EXCEPT), FE_OVERFLOW | FE_INEXACT);
    }
  }
}

TEST(LlvmLibcBesselTest, IndependentMPFRScaledError) {
  struct Function {
    double (*actual)(double);
    int (*reference)(mpfr_ptr, mpfr_srcptr, mpfr_rnd_t);
    bool second_kind;
  };
  const Function functions[] = {{LIBC_NAMESPACE::j0, mpfr_j0, false},
                                {LIBC_NAMESPACE::j1, mpfr_j1, false},
                                {LIBC_NAMESPACE::y0, mpfr_y0, true},
                                {LIBC_NAMESPACE::y1, mpfr_y1, true}};
  mpfr_t input, reference, more, rounded, actual, scale, error;
  mpfr_inits2(512, input, reference, rounded, (mpfr_ptr)0);
  mpfr_inits2(1024, more, actual, scale, error, (mpfr_ptr)0);
  const uint64_t special[] = {1,
                              2,
                              0x0010000000000000,
                              0x3c90000000000000,
                              0x3e40000000000000,
                              0x3f20000000000000,
                              0x3ff0000000000000,
                              0x4000000000000000,
                              0x4006db6d00000000,
                              0x40122e8b00000000,
                              0x4020000000000000,
                              0x4800000000000000,
                              0x7fe0000000000000,
                              0x7feffffffffffffe};
  auto check = [&](double x, const Function &function) {
    mpfr_set_d(input, x, MPFR_RNDN);
    function.reference(reference, input, MPFR_RNDN);
    function.reference(more, input, MPFR_RNDN);
    mpfr_set(rounded, more, MPFR_RNDN);
    ASSERT_TRUE(mpfr_equal_p(reference, rounded) != 0);
    double result = function.actual(x);
    double expected = mpfr_get_d(more, MPFR_RNDN);
    if (Bits(expected).is_inf()) {
      EXPECT_EQ(Bits(result).uintval(), Bits(expected).uintval());
      return;
    }
    mpfr_set_d(actual, result, MPFR_RNDN);
    mpfr_sub(error, actual, more, MPFR_RNDN);
    mpfr_abs(error, error, MPFR_RNDN);
    mpfr_abs(scale, input, MPFR_RNDN);
    mpfr_sqrt(scale, scale, MPFR_RNDN);
    mpfr_ui_div(scale, 1, scale, MPFR_RNDN);
    if (mpfr_cmp_ui(scale, 1) > 0)
      mpfr_set_ui(scale, 1, MPFR_RNDN);
    mpfr_abs(actual, more, MPFR_RNDN);
    if (mpfr_cmp(scale, actual) < 0)
      mpfr_set(scale, actual, MPFR_RNDN);
    mpfr_mul_2si(scale, scale, -48, MPFR_RNDN);
    EXPECT_TRUE(mpfr_number_p(error) && mpfr_cmp(error, scale) <= 0);
  };
  for (const auto &function : functions) {
    for (uint64_t bits : special)
      for (uint64_t offset = 0; offset < 2; ++offset) {
        double x = Bits(bits + offset).get_val();
        check(x, function);
        if (!function.second_kind)
          check(-x, function);
      }
    // Adjacent binary64 inputs near roots of all four functions.
    constexpr double roots[] = {2.4048255576957728, 5.5200781102863106,
                                3.8317059702075123, 7.0155866698156188,
                                0.8935769662791675, 3.9576784193148579,
                                2.1971413260310170, 5.4296810407941351};
    for (double root : roots) {
      uint64_t bits = Bits(root).uintval();
      for (uint64_t adjacent = bits - 2; adjacent <= bits + 2; ++adjacent)
        check(Bits(adjacent).get_val(), function);
    }
    // Sample each approximation region and argument reduction at large scales.
    for (unsigned i = 1; i <= 128; ++i) {
      check(double(i) * 0.125, function);
      uint64_t bits = (uint64_t(1 + (i * 17) % 2046) << 52) | 0x921fb54442d18;
      check(Bits(bits).get_val(), function);
    }
  }
  mpfr_clears(input, reference, more, rounded, actual, scale, error,
              (mpfr_ptr)0);
}
