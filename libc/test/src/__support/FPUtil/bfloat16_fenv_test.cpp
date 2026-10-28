//===-- Tests for emulated BFloat16 exception flags
//------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/FPUtil/BasicOperations.h"
#include "src/__support/FPUtil/bfloat16.h"
#include "src/__support/FPUtil/dyadic_float.h"
#include "src/__support/FPUtil/generic/add_sub.h"
#include "test/UnitTest/FEnvSafeTest.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/RoundingModeUtils.h"

namespace fp = LIBC_NAMESPACE::fputil;
using LIBC_NAMESPACE::Sign;
using T = fp::BFloat16;
using Bits = fp::FPBits<T>;
using fp::testing::ForceRoundingMode;
using fp::testing::RoundingMode;
using LlvmLibcBFloat16FEnvTest = LIBC_NAMESPACE::testing::FEnvSafeTest;

static constexpr RoundingMode MODES[] = {
    RoundingMode::Nearest,
#ifndef LIBC_MATH_HAS_ASSUME_ROUND_NEAREST_ONLY
    RoundingMode::Upward,
    RoundingMode::Downward,
    RoundingMode::TowardZero,
#endif
};

#if LIBC_HAS_CONSTANT_EVALUATION
static_assert(Bits(T(1)).uintval() == Bits::one().uintval());
static_assert(Bits(T(1.0f)).uintval() == Bits::one().uintval());
static_assert(Bits(T(0x1.01p0f)).uintval() == Bits::one().uintval());
static_assert(Bits(fp::generic::sub<T>(Bits::zero().get_val(),
                                       Bits::zero().get_val()))
                  .uintval() == Bits::zero().uintval());
static_assert(Bits(fp::generic::sub<T>(Bits::one().get_val(),
                                       Bits::one().get_val()))
                  .uintval() == Bits::zero().uintval());
#endif

template <typename Function>
void expect_preserved(Bits expected, Function operation) {
  const int previous_flags[] = {0, FE_INVALID, FE_INEXACT | FE_DIVBYZERO};
  for (int previous : previous_flags) {
    fp::clear_except(FE_ALL_EXCEPT);
    fp::raise_except(previous);
    libc_errno = EDOM;
    T result = operation();
    EXPECT_EQ(Bits(result).uintval(), expected.uintval());
    EXPECT_EQ(fp::test_except(FE_ALL_EXCEPT), previous);
    EXPECT_MATH_ERRNO(EDOM);
  }
}

TEST_F(LlvmLibcBFloat16FEnvTest, ExactConversions) {
  for (auto mode : MODES) {
    ForceRoundingMode rounding(mode);
    if (!rounding.success)
      continue;
    expect_preserved(Bits::one(), [] { return T(1); });
    expect_preserved(Bits::one(), [] { return T(1.0f); });
    expect_preserved(Bits::one(), [] { return T(1.0); });
    const Bits inputs[] = {Bits::zero(),          Bits::zero(Sign::NEG),
                           Bits::min_subnormal(), Bits::min_normal(),
                           Bits::max_normal(),    Bits::one(Sign::NEG)};
    for (auto input : inputs) {
      expect_preserved(input, [=] { return T(float(input.get_val())); });
      fp::DyadicFloat<128> exact(input.get_val());
      expect_preserved(input,
                       [&] { return exact.template generic_as<T, true>(); });
    }
  }
}

TEST_F(LlvmLibcBFloat16FEnvTest, ExactArithmetic) {
  const T zero = Bits::zero().get_val(), one = Bits::one().get_val();
  const T inf = Bits::inf().get_val();
  Bits half_max_bits = Bits::max_normal();
  half_max_bits.set_biased_exponent(half_max_bits.get_biased_exponent() - 1);
  const T half_max = half_max_bits.get_val();
  for (auto mode : MODES) {
    ForceRoundingMode rounding(mode);
    if (!rounding.success)
      continue;
    const Bits cancelled =
        Bits::zero(mode == RoundingMode::Downward ? Sign::NEG : Sign::POS);
    expect_preserved(cancelled, [&] { return zero - zero; });
    expect_preserved(cancelled, [&] { return one - one; });
    expect_preserved(cancelled, [&] { return one + -one; });
    expect_preserved(Bits::zero(), [&] { return fp::fdim(inf, inf); });
    expect_preserved(Bits::zero(), [&] { return fp::fdim(one, one); });
    expect_preserved(Bits::max_normal(),
                     [&] { return fp::fdim(half_max, -half_max); });
  }
}

TEST_F(LlvmLibcBFloat16FEnvTest, InexactConversionPolicy) {
  fp::DyadicFloat<128> halfway(0x1.01p0f);
  const Sign signs[] = {Sign::POS, Sign::NEG};
  for (auto mode : MODES) {
    ForceRoundingMode rounding(mode);
    if (!rounding.success)
      continue;
    for (auto sign : signs) {
      halfway.sign = sign;
      const bool away = (mode == RoundingMode::Upward && sign.is_pos()) ||
                        (mode == RoundingMode::Downward && sign.is_neg());
      const Bits expected(uint16_t(Bits::one(sign).uintval() + int(away)));
      expect_preserved(expected,
                       [&] { return halfway.template generic_as<T, false>(); });
      fp::clear_except(FE_ALL_EXCEPT);
      libc_errno = EDOM;
      T result = halfway.template generic_as<T, true>();
      EXPECT_EQ(Bits(result).uintval(), expected.uintval());
      EXPECT_FP_EXCEPTION(FE_INEXACT);
      EXPECT_MATH_ERRNO(EDOM);
    }
  }
}

TEST_F(LlvmLibcBFloat16FEnvTest, RangeErrors) {
  fp::DyadicFloat<128> overflow(Bits::max_normal().get_val());
  ++overflow.exponent;
  fp::DyadicFloat<128> underflow(Bits::min_subnormal().get_val());
  --underflow.exponent;
  for (auto mode : MODES) {
    ForceRoundingMode rounding(mode);
    if (!rounding.success)
      continue;
    const bool up = mode == RoundingMode::Upward;
    const bool saturates =
        mode == RoundingMode::Downward || mode == RoundingMode::TowardZero;
    expect_preserved(saturates ? Bits::max_normal() : Bits::inf(),
                     [&] { return overflow.template generic_as<T, false>(); });
    expect_preserved(up ? Bits::min_subnormal() : Bits::zero(),
                     [&] { return underflow.template generic_as<T, false>(); });
    fp::clear_except(FE_ALL_EXCEPT);
    libc_errno = 0;
    T result = overflow.template generic_as<T, true>();
    EXPECT_EQ(Bits(result).uintval(),
              (saturates ? Bits::max_normal() : Bits::inf()).uintval());
    EXPECT_FP_EXCEPTION(FE_OVERFLOW | FE_INEXACT);
    EXPECT_MATH_ERRNO(ERANGE);
    fp::clear_except(FE_ALL_EXCEPT);
    result = underflow.template generic_as<T, true>();
    EXPECT_EQ(Bits(result).uintval(),
              (up ? Bits::min_subnormal() : Bits::zero()).uintval());
    EXPECT_FP_EXCEPTION(FE_UNDERFLOW | FE_INEXACT);
    EXPECT_MATH_ERRNO(ERANGE);
  }
}
