//===-- Utility class to test different flavors of fdim ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/CPP/algorithm.h"
#include "src/__support/FPUtil/FPBits.h"
#include "test/UnitTest/FEnvSafeTest.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::Sign;

template <typename T>
class FDimTestTemplate : public LIBC_NAMESPACE::testing::FEnvSafeTest {
public:
  using FuncPtr = T (*)(T, T);
  using FPBits = LIBC_NAMESPACE::fputil::FPBits<T>;
  using StorageType = typename FPBits::StorageType;

  const T inf = FPBits::inf(Sign::POS).get_val();
  const T neg_inf = FPBits::inf(Sign::NEG).get_val();
  const T zero = FPBits::zero(Sign::POS).get_val();
  const T neg_zero = FPBits::zero(Sign::NEG).get_val();
  const T nan = FPBits::quiet_nan().get_val();

  void test_nan_arg(FuncPtr func) {
    EXPECT_FP_EQ(nan, func(nan, inf));
    EXPECT_FP_EQ(nan, func(neg_inf, nan));
    EXPECT_FP_EQ(nan, func(nan, zero));
    EXPECT_FP_EQ(nan, func(neg_zero, nan));
    EXPECT_FP_EQ(nan, func(nan, T(-1.2345)));
    EXPECT_FP_EQ(nan, func(T(1.2345), nan));
    EXPECT_FP_EQ(func(nan, nan), nan);
  }

  void test_inf_arg(FuncPtr func) {
    EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(zero, func(inf, inf), 0);
    EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(zero, func(neg_inf, neg_inf), 0);
    EXPECT_FP_EQ_WITH_EXCEPTION_ALL_ROUNDING(
        zero, func(T(-1), FPBits::min_subnormal().get_val()), 0);
    EXPECT_FP_EQ(zero, func(neg_inf, inf));
    EXPECT_FP_EQ(inf, func(inf, zero));
    EXPECT_FP_EQ(zero, func(neg_zero, inf));
    EXPECT_FP_EQ(inf, func(inf, T(1.2345)));
    EXPECT_FP_EQ(zero, func(T(-1.2345), inf));
  }

  void test_neg_inf_arg(FuncPtr func) {
    EXPECT_FP_EQ(inf, func(inf, neg_inf));
    EXPECT_FP_EQ(zero, func(neg_inf, zero));
    EXPECT_FP_EQ(inf, func(neg_zero, neg_inf));
    EXPECT_FP_EQ(zero, func(neg_inf, T(-1.2345)));
    EXPECT_FP_EQ(inf, func(T(1.2345), neg_inf));
  }

  void test_both_zero(FuncPtr func) {
    EXPECT_FP_EQ(zero, func(zero, zero));
    EXPECT_FP_EQ(zero, func(zero, neg_zero));
    EXPECT_FP_EQ(zero, func(neg_zero, zero));
    EXPECT_FP_EQ(zero, func(neg_zero, neg_zero));
  }

  void test_range_errors(FuncPtr func) {
    using namespace LIBC_NAMESPACE::fputil::testing;
    const T max = FPBits::max_normal().get_val();
    FPBits step_bits = FPBits::max_normal();
    step_bits.set_mantissa(0);
    step_bits.set_biased_exponent(step_bits.get_biased_exponent() -
                                  FPBits::FRACTION_LEN);
    const T step = step_bits.get_val();
    FPBits large_bits = FPBits::max_normal();
    large_bits.set_mantissa(StorageType(1) << (FPBits::FRACTION_LEN - 1));
    FPBits small_bits = FPBits::max_normal();
    small_bits.set_mantissa(0);
    small_bits.set_biased_exponent(small_bits.get_biased_exponent() - 1);
    const T large = large_bits.get_val(), small = small_bits.get_val();
    FPBits half_max_bits = FPBits::max_normal();
    half_max_bits.set_biased_exponent(half_max_bits.get_biased_exponent() - 1);
    const T half_max = half_max_bits.get_val();
    const RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::Upward,
                                  RoundingMode::Downward,
                                  RoundingMode::TowardZero};
    for (auto mode : modes) {
      ForceRoundingMode rounding(mode);
      if (!rounding.success)
        continue;
      const bool saturates =
          mode == RoundingMode::Downward || mode == RoundingMode::TowardZero;
      const T operands[] = {step, max};
      for (T x : operands) {
        libc_errno = 0;
        EXPECT_FP_EQ_WITH_EXCEPTION(saturates ? max : inf, func(x, -max),
                                    FE_OVERFLOW | FE_INEXACT);
        EXPECT_MATH_ERRNO(ERANGE);
        EXPECT_FP_EQ_WITH_EXCEPTION(saturates ? max : inf, func(max, -x),
                                    FE_OVERFLOW | FE_INEXACT);
        EXPECT_MATH_ERRNO(ERANGE);
      }
      EXPECT_FP_EQ_WITH_EXCEPTION(saturates ? max : inf, func(large, -small),
                                  FE_OVERFLOW | FE_INEXACT);
      EXPECT_MATH_ERRNO(ERANGE);
      EXPECT_FP_EQ(max, func(half_max, -half_max));
      EXPECT_MATH_ERRNO(0);
      // Below the overflow threshold, directed finite rounding is not an error.
      libc_errno = 0;
      const T tiny = FPBits::min_subnormal().get_val();
      EXPECT_FP_EQ_WITH_EXCEPTION(
          mode == RoundingMode::Upward ? inf : max, func(max, -tiny),
          mode == RoundingMode::Upward ? FE_OVERFLOW | FE_INEXACT : FE_INEXACT);
      EXPECT_MATH_ERRNO(mode == RoundingMode::Upward ? ERANGE : 0);

      // Preexisting exception flags must not turn an exact result into an
      // error.
      LIBC_NAMESPACE::fputil::raise_except(FE_OVERFLOW);
      libc_errno = EDOM;
      EXPECT_FP_EQ(max, func(max, zero));
      EXPECT_MATH_ERRNO(EDOM);
      libc_errno = EDOM;
      EXPECT_FP_EQ(inf, func(inf, T(1)));
      EXPECT_MATH_ERRNO(EDOM);
      libc_errno = EDOM;
      EXPECT_FP_EQ(inf, func(T(1), neg_inf));
      EXPECT_MATH_ERRNO(EDOM);
      libc_errno = EDOM;
      EXPECT_FP_EQ(nan, func(nan, -max));
      EXPECT_MATH_ERRNO(EDOM);
    }
  }

  void test_in_range(FuncPtr func) {
    constexpr StorageType STORAGE_MAX =
        LIBC_NAMESPACE::cpp::numeric_limits<StorageType>::max();
    constexpr int COUNT = 1'231;
    constexpr StorageType STEP = LIBC_NAMESPACE::cpp::max(
        static_cast<StorageType>(STORAGE_MAX / COUNT), StorageType(1));
    StorageType v = 0, w = STORAGE_MAX;
    for (int i = 0; i <= COUNT; ++i, v += STEP, w -= STEP) {
      FPBits xbits(v), ybits(w);
      if (xbits.is_inf_or_nan())
        continue;
      if (ybits.is_inf_or_nan())
        continue;

      T x = xbits.get_val();
      T y = ybits.get_val();

      if (x > y) {
        EXPECT_FP_EQ(x - y, func(x, y));
      } else {
        EXPECT_FP_EQ(zero, func(x, y));
      }
    }
  }
};

#define LIST_FDIM_TESTS(T, func)                                               \
  using LlvmLibcFDimTest = FDimTestTemplate<T>;                                \
  TEST_F(LlvmLibcFDimTest, NaNArg) { test_nan_arg(&func); }                    \
  TEST_F(LlvmLibcFDimTest, InfArg) { test_inf_arg(&func); }                    \
  TEST_F(LlvmLibcFDimTest, NegInfArg) { test_neg_inf_arg(&func); }             \
  TEST_F(LlvmLibcFDimTest, BothZero) { test_both_zero(&func); }                \
  TEST_F(LlvmLibcFDimTest, RangeErrors) { test_range_errors(&func); }          \
  TEST_F(LlvmLibcFDimTest, InFloatRange) { test_in_range(&func); }             \
  static_assert(true, "Require semicolon.")
