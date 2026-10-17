//===-- Binary64 long double wrapper tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/macros/properties/types.h"

#if defined(LIBC_TYPES_LONG_DOUBLE_IS_FLOAT64)
#include "src/__support/CPP/bit.h"
#include "src/__support/complex_type.h"
#include "src/complex/cabs.h"
#include "src/complex/cabsl.h"
#include "src/complex/cacos.h"
#include "src/complex/cacosh.h"
#include "src/complex/cacoshl.h"
#include "src/complex/cacosl.h"
#include "src/complex/carg.h"
#include "src/complex/cargl.h"
#include "src/complex/casin.h"
#include "src/complex/casinh.h"
#include "src/complex/casinhl.h"
#include "src/complex/casinl.h"
#include "src/complex/catan.h"
#include "src/complex/catanh.h"
#include "src/complex/catanhl.h"
#include "src/complex/catanl.h"
#include "src/complex/ccos.h"
#include "src/complex/ccosh.h"
#include "src/complex/ccoshl.h"
#include "src/complex/ccosl.h"
#include "src/complex/cexp.h"
#include "src/complex/cexpl.h"
#include "src/complex/clog.h"
#include "src/complex/clogl.h"
#include "src/complex/cpow.h"
#include "src/complex/cpowl.h"
#include "src/complex/csin.h"
#include "src/complex/csinh.h"
#include "src/complex/csinhl.h"
#include "src/complex/csinl.h"
#include "src/complex/csqrt.h"
#include "src/complex/csqrtl.h"
#include "src/complex/ctan.h"
#include "src/complex/ctanh.h"
#include "src/complex/ctanhl.h"
#include "src/complex/ctanl.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

// These tests check delegation, not the accuracy of the double providers.
class LlvmLibcComplexBinary64LongDoubleTest
    : public LIBC_NAMESPACE::testing::FPTest<long double> {
protected:
  void compare(long double expected, long double actual) {
    EXPECT_FP_EQ(expected, actual);
  }
  void compare(_Complex long double expected, _Complex long double actual) {
    EXPECT_CFP_EQ(expected, actual);
  }

  static _Complex long double makeComplex(long double real, long double imag) {
    return LIBC_NAMESPACE::cpp::bit_cast<_Complex long double>(
        LIBC_NAMESPACE::Complex<long double>{real, imag});
  }

  template <typename Reference, typename Wrapper>
  void check(Reference reference, Wrapper wrapper) {
    for (auto mode : ROUNDING_MODES) {
      LIBC_NAMESPACE::fputil::testing::ForceRoundingMode rounding(mode);
      if (!rounding.success)
        continue;
      LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
      libc_errno = 123;
      auto expected = reference();
      int expected_errno = libc_errno;
      int expected_flags = LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT);
      LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
      libc_errno = 123;
      auto actual = wrapper();
      int actual_errno = libc_errno;
      int actual_flags = LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT);
      compare(expected, actual);
      EXPECT_EQ(expected_errno, actual_errno);
      EXPECT_EQ(expected_flags, actual_flags);
    }
    libc_errno = 0;
    LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
  }
};

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, cabsl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::cabs(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::cabsl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, cargl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::carg(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::cargl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, cacosl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::cacos(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::cacosl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, casinl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::casin(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::casinl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, catanl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::catan(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::catanl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, ccosl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::ccos(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::ccosl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, csinl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::csin(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::csinl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, ctanl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::ctan(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::ctanl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, cacoshl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::cacosh(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::cacoshl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, casinhl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::casinh(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::casinhl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, catanhl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::catanh(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::catanhl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, ccoshl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::ccosh(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::ccoshl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, csinhl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::csinh(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::csinhl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, ctanhl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::ctanh(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::ctanhl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, cexpl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::cexp(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::cexpl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, clogl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::clog(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::clogl(x); });
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, cpowl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  const _Complex long double exponents[] = {makeComplex(2.0L, -0.5L),
                                            makeComplex(zero, zero)};
  for (auto x : inputs) {
    for (auto y : exponents) {
      check(
          [&] {
            return static_cast<_Complex long double>(
                LIBC_NAMESPACE::cpow(static_cast<_Complex double>(x),
                                     static_cast<_Complex double>(y)));
          },
          [&] { return LIBC_NAMESPACE::cpowl(x, y); });
    }
  }
}

TEST_F(LlvmLibcComplexBinary64LongDoubleTest, csqrtl) {
  const _Complex long double inputs[] = {
      makeComplex(0.5L, 0.25L),
      makeComplex(-2.0L, 0.5L),
      makeComplex(zero, zero),
      makeComplex(neg_zero, neg_zero),
      makeComplex(-2.0L, zero),
      makeComplex(-2.0L, neg_zero),
      makeComplex(zero, 2.0L),
      makeComplex(neg_zero, 2.0L),
      makeComplex(min_denormal, -min_denormal),
      makeComplex(inf, zero),
      makeComplex(zero, neg_inf),
      makeComplex(aNaN, 1.0L),
      makeComplex(1.0L, aNaN)};
  for (auto x : inputs) {
    check(
        [&] {
          return static_cast<_Complex long double>(
              LIBC_NAMESPACE::csqrt(static_cast<_Complex double>(x)));
        },
        [&] { return LIBC_NAMESPACE::csqrtl(x); });
  }
}

#endif // LIBC_TYPES_LONG_DOUBLE_IS_FLOAT64
