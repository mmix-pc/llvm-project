//===-- Tests for DyadicFloat range errors --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/FPUtil/dyadic_float.h"
#include "src/__support/FPUtil/generic/div.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/FEnvSafeTest.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"

namespace fp = LIBC_NAMESPACE::fputil;
using fp::testing::RoundingMode;
using LIBC_NAMESPACE::Sign;
using LlvmLibcDyadicFloatRangeTest = LIBC_NAMESPACE::testing::FEnvSafeTest;

template <typename T, bool Division> void test_overflow() {
  using Bits = fp::FPBits<T>;
  using Dyadic = fp::DyadicFloat<128>;
  constexpr RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::Upward,
                                    RoundingMode::Downward,
                                    RoundingMode::TowardZero};
  constexpr Sign signs[] = {Sign::POS, Sign::NEG};
  const int previous_flags[] = {0, FE_OVERFLOW};
  for (auto mode : modes) {
    fp::testing::ForceRoundingMode guard(mode);
    ASSERT_TRUE(guard.success);
    for (Sign sign : signs) {
      bool finite = mode == RoundingMode::TowardZero ||
                    (mode == RoundingMode::Upward && sign == Sign::NEG) ||
                    (mode == RoundingMode::Downward && sign == Sign::POS);
      Bits expected = finite ? Bits::max_normal(sign) : Bits::inf(sign);
      for (int previous : previous_flags) {
        fp::clear_except(FE_ALL_EXCEPT);
        fp::raise_except(previous);
        libc_errno = 0;
        T result;
        if constexpr (Division) {
          result =
              fp::generic::div<T>(Bits::max_normal(sign).get_val(), T(0.5));
        } else {
          Dyadic value(Bits::max_normal(sign).get_val());
          ++value.exponent;
          result = value.template fast_as<T, true>();
        }
        EXPECT_EQ(Bits(result).uintval(), expected.uintval());
        EXPECT_EQ(int(libc_errno), ERANGE);
        EXPECT_EQ(fp::test_except(FE_ALL_EXCEPT), FE_OVERFLOW | FE_INEXACT);
      }
    }
  }
}

TEST_F(LlvmLibcDyadicFloatRangeTest, FastOverflow) {
  test_overflow<float, false>();
  test_overflow<double, false>();
}

TEST_F(LlvmLibcDyadicFloatRangeTest, DivisionOverflow) {
  test_overflow<float, true>();
  test_overflow<double, true>();
}

TEST_F(LlvmLibcDyadicFloatRangeTest, ErrnoPolicy) {
  using Bits = fp::FPBits<double>;
  fp::testing::ForceRoundingMode guard(RoundingMode::Nearest);
  ASSERT_TRUE(guard.success);
  fp::DyadicFloat<128> value(Bits::max_normal().get_val());
  libc_errno = EDOM;
  fp::clear_except(FE_ALL_EXCEPT);
  fp::raise_except(FE_INVALID);
  double normal = value.fast_as<double, true>();
  EXPECT_EQ(Bits(normal).uintval(), Bits::max_normal().uintval());
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(fp::test_except(FE_ALL_EXCEPT), FE_INVALID);
  ++value.exponent;
  double overflow = value.fast_as<double, false>();
  EXPECT_TRUE(Bits(overflow).is_inf());
  EXPECT_EQ(int(libc_errno), EDOM);
}

TEST_F(LlvmLibcDyadicFloatRangeTest, RoundedOverflow) {
  using Bits = fp::FPBits<double>;
  using Dyadic = fp::DyadicFloat<128>;
  fp::testing::ForceRoundingMode guard(RoundingMode::Nearest);
  ASSERT_TRUE(guard.success);
  Dyadic value(Bits::max_normal().get_val());
  // Half an ulp beyond max_normal rounds up to infinity.
  value.mantissa += Dyadic::MantissaType(1) << (128 - Bits::FRACTION_LEN - 2);
  const Sign signs[] = {Sign::POS, Sign::NEG};
  for (Sign sign : signs) {
    value.sign = sign;
    libc_errno = 0;
    fp::clear_except(FE_ALL_EXCEPT);
    double result = value.fast_as<double, true>();
    EXPECT_EQ(Bits(result).uintval(), Bits::inf(sign).uintval());
    EXPECT_EQ(int(libc_errno), ERANGE);
    EXPECT_EQ(fp::test_except(FE_ALL_EXCEPT), FE_OVERFLOW | FE_INEXACT);
  }
}
