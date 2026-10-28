//===-- Utility class to test different flavors of hypot ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TEST_SRC_MATH_HYPOTTEST_H
#define LLVM_LIBC_TEST_SRC_MATH_HYPOTTEST_H

#include "src/__support/macros/properties/architectures.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

template <typename T>
struct HypotTestTemplate : public LIBC_NAMESPACE::testing::Test {
  using Func = T (*)(T, T);

  DECLARE_SPECIAL_CONSTANTS(T)

  void test_large_finite_flags(Func func, T scale) {
    using namespace LIBC_NAMESPACE::fputil::testing;
    for (auto mode : LIBC_NAMESPACE::testing::FPTest<T>::ROUNDING_MODES) {
      ForceRoundingMode rounding(mode);
      if (!rounding.success)
        continue;
      LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
      LIBC_NAMESPACE::fputil::raise_except(FE_INVALID);
      libc_errno = EDOM;
      T result = func(T(4) * scale, T(4) * scale);
      int flags = LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT);
      EXPECT_TRUE(result > T(5) * scale && result < T(6) * scale);
      EXPECT_MATH_ERRNO(EDOM);
      if (math_errhandling & MATH_ERREXCEPT)
        EXPECT_EQ(FE_INVALID | FE_INEXACT, flags);

      LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
      LIBC_NAMESPACE::fputil::raise_except(FE_INVALID);
      libc_errno = EDOM;
      result = func(T(3) * scale, T(4) * scale);
      flags = LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT);
      EXPECT_FP_EQ(T(5) * scale, result);
      EXPECT_MATH_ERRNO(EDOM);
      // Rounding-mode detection may itself raise the permitted inexact flag.
      if (math_errhandling & MATH_ERREXCEPT)
        EXPECT_EQ(FE_INVALID, flags & ~FE_INEXACT);

      LIBC_NAMESPACE::fputil::raise_except(FE_INEXACT);
      result = func(T(3) * scale, T(4) * scale);
      flags = LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT);
      EXPECT_FP_EQ(T(5) * scale, result);
      if (math_errhandling & MATH_ERREXCEPT)
        EXPECT_EQ(FE_INVALID | FE_INEXACT, flags);
    }
  }

  void test_overflow(Func func) {
    using namespace LIBC_NAMESPACE::fputil::testing;
    for (auto mode : LIBC_NAMESPACE::testing::FPTest<T>::ROUNDING_MODES) {
      ForceRoundingMode rounding(mode);
      if (!rounding.success)
        continue;
      const T signs[] = {T(1), T(-1)};
      for (T sign : signs) {
        LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
        LIBC_NAMESPACE::fputil::raise_except(FE_INVALID);
        libc_errno = 0;
        T expected =
            mode == RoundingMode::TowardZero || mode == RoundingMode::Downward
                ? max_normal
                : inf;
        EXPECT_FP_EQ(expected, func(sign * max_normal, max_normal));
        EXPECT_MATH_ERRNO(ERANGE);
        if (math_errhandling & MATH_ERREXCEPT)
          EXPECT_EQ(FE_INVALID | FE_OVERFLOW | FE_INEXACT,
                    LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT));
      }
      // A preexisting overflow flag must not report an error for valid inputs.
      libc_errno = EDOM;
      EXPECT_FP_EQ(max_normal, func(max_normal, T(0)));
      EXPECT_MATH_ERRNO(EDOM);
      libc_errno = EDOM;
      EXPECT_FP_EQ(inf, func(inf, max_normal));
      EXPECT_MATH_ERRNO(EDOM);
    }
  }

  void test_special_numbers(Func func) {
    constexpr int N = 4;
    // Pythagorean triples.
    constexpr T PYT[N][3] = {{T(3), T(4), T(5)},
                             {T(5), T(12), T(13)},
                             {T(8), T(15), T(17)},
                             {T(7), T(24), T(25)}};

#ifndef LIBC_TARGET_ARCH_IS_NVPTX
    // TODO: Investigate why sNaN tests are failing on nVidia.
    // https://github.com/llvm/llvm-project/issues/99706.
    EXPECT_FP_EQ(func(inf, sNaN), aNaN);
    EXPECT_FP_EQ(func(sNaN, neg_inf), aNaN);
#endif // !LIBC_TARGET_ARCH_IS_NVPTX

    EXPECT_FP_EQ(func(inf, aNaN), inf);
    EXPECT_FP_EQ(func(aNaN, neg_inf), inf);
    EXPECT_FP_EQ(func(aNaN, aNaN), aNaN);
    EXPECT_FP_EQ(func(aNaN, zero), aNaN);
    EXPECT_FP_EQ(func(neg_zero, aNaN), aNaN);

    for (int i = 0; i < N; ++i) {
      EXPECT_FP_EQ_ALL_ROUNDING(PYT[i][2], func(PYT[i][0], PYT[i][1]));
      EXPECT_FP_EQ_ALL_ROUNDING(PYT[i][2], func(-PYT[i][0], PYT[i][1]));
      EXPECT_FP_EQ_ALL_ROUNDING(PYT[i][2], func(PYT[i][0], -PYT[i][1]));
      EXPECT_FP_EQ_ALL_ROUNDING(PYT[i][2], func(-PYT[i][0], -PYT[i][1]));

      EXPECT_FP_EQ_ALL_ROUNDING(PYT[i][2], func(PYT[i][1], PYT[i][0]));
      EXPECT_FP_EQ_ALL_ROUNDING(PYT[i][2], func(-PYT[i][1], PYT[i][0]));
      EXPECT_FP_EQ_ALL_ROUNDING(PYT[i][2], func(PYT[i][1], -PYT[i][0]));
      EXPECT_FP_EQ_ALL_ROUNDING(PYT[i][2], func(-PYT[i][1], -PYT[i][0]));
    }
  }
};

#endif // LLVM_LIBC_TEST_SRC_MATH_HYPOTTEST_H
