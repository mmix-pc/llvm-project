//===-- Binary64 long double wrapper tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/macros/properties/types.h"

#if defined(LIBC_TYPES_LONG_DOUBLE_IS_FLOAT64)
#include "src/math/acos.h"
#include "src/math/acosh.h"
#include "src/math/acoshl.h"
#include "src/math/acosl.h"
#include "src/math/asin.h"
#include "src/math/asinh.h"
#include "src/math/asinhl.h"
#include "src/math/asinl.h"
#include "src/math/atan.h"
#include "src/math/atanh.h"
#include "src/math/atanhl.h"
#include "src/math/atanl.h"
#include "src/math/cbrt.h"
#include "src/math/cbrtl.h"
#include "src/math/cos.h"
#include "src/math/cosh.h"
#include "src/math/coshl.h"
#include "src/math/cosl.h"
#include "src/math/erf.h"
#include "src/math/erfc.h"
#include "src/math/erfcl.h"
#include "src/math/erfl.h"
#include "src/math/exp.h"
#include "src/math/exp2.h"
#include "src/math/exp2l.h"
#include "src/math/expl.h"
#include "src/math/expm1.h"
#include "src/math/expm1l.h"
#include "src/math/fma.h"
#include "src/math/fmal.h"
#include "src/math/hypot.h"
#include "src/math/hypotl.h"
#include "src/math/lgamma.h"
#include "src/math/lgammal.h"
#include "src/math/log.h"
#include "src/math/log10.h"
#include "src/math/log10l.h"
#include "src/math/log1p.h"
#include "src/math/log1pl.h"
#include "src/math/log2.h"
#include "src/math/log2l.h"
#include "src/math/logl.h"
#include "src/math/pow.h"
#include "src/math/powl.h"
#include "src/math/sin.h"
#include "src/math/sinh.h"
#include "src/math/sinhl.h"
#include "src/math/sinl.h"
#include "src/math/tan.h"
#include "src/math/tanh.h"
#include "src/math/tanhl.h"
#include "src/math/tanl.h"
#include "src/math/tgamma.h"
#include "src/math/tgammal.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

// These tests check delegation, not the accuracy of the double providers.
class LlvmLibcBinary64LongDoubleTest
    : public LIBC_NAMESPACE::testing::FPTest<long double> {
protected:
  void compare(long double expected, long double actual) {
    EXPECT_FP_EQ(expected, actual);
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

TEST_F(LlvmLibcBinary64LongDoubleTest, acosl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::acos(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::acosl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, asinl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::asin(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::asinl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, atanl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::atan(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::atanl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, cosl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::cos(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::cosl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, sinl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::sin(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::sinl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, tanl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::tan(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::tanl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, acoshl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::acosh(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::acoshl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, asinhl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::asinh(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::asinhl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, atanhl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::atanh(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::atanhl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, coshl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::cosh(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::coshl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, sinhl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::sinh(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::sinhl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, tanhl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::tanh(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::tanhl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, expl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::exp(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::expl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, exp2l) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::exp2(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::exp2l(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, expm1l) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::expm1(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::expm1l(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, logl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::log(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::logl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, log10l) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::log10(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::log10l(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, log1pl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::log1p(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::log1pl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, log2l) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::log2(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::log2l(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, cbrtl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::cbrt(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::cbrtl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, hypotl) {
  const long double inputs[][2] = {{2.0L, 3.0L},       {-2.0L, 3.0L},
                                   {neg_zero, 3.0L},   {zero, -1.0L},
                                   {max_normal, 2.0L}, {min_denormal, 2.0L},
                                   {inf, 2.0L},        {2.0L, neg_inf},
                                   {aNaN, 1.0L},       {1.0L, aNaN}};
  for (const auto &v : inputs)
    check(
        [&] {
          return static_cast<long double>(LIBC_NAMESPACE::hypot(
              static_cast<double>(v[0]), static_cast<double>(v[1])));
        },
        [&] { return LIBC_NAMESPACE::hypotl(v[0], v[1]); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, powl) {
  const long double inputs[][2] = {{2.0L, 3.0L},       {-2.0L, 3.0L},
                                   {neg_zero, 3.0L},   {zero, -1.0L},
                                   {max_normal, 2.0L}, {min_denormal, 2.0L},
                                   {inf, 2.0L},        {2.0L, neg_inf},
                                   {aNaN, 1.0L},       {1.0L, aNaN}};
  for (const auto &v : inputs)
    check(
        [&] {
          return static_cast<long double>(LIBC_NAMESPACE::pow(
              static_cast<double>(v[0]), static_cast<double>(v[1])));
        },
        [&] { return LIBC_NAMESPACE::powl(v[0], v[1]); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, erfl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::erf(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::erfl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, erfcl) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::erfc(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::erfcl(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, lgammal) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::lgamma(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::lgammal(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, tgammal) {
  const long double inputs[] = {
      zero,       neg_zero,   0.5L,  -0.5L,        1.0L,
      -1.0L,      2.0L,       -2.0L, min_denormal, -min_denormal,
      min_normal, max_normal, inf,   neg_inf,      aNaN};
  for (long double x : inputs)
    check(
        [&] {
          return static_cast<long double>(
              LIBC_NAMESPACE::tgamma(static_cast<double>(x)));
        },
        [&] { return LIBC_NAMESPACE::tgammal(x); });
}

TEST_F(LlvmLibcBinary64LongDoubleTest, fmal) {
  const long double inputs[][3] = {
      {0x1.0000000000001p0L, 0x1.fffffffffffffp-1L, -1.0L},
      {max_normal, 2.0L, -max_normal},
      {min_denormal, 0.5L, zero},
      {neg_zero, 1.0L, neg_zero},
      {inf, zero, 1.0L},
      {aNaN, 1.0L, 2.0L}};
  for (const auto &v : inputs)
    check(
        [&] {
          return static_cast<long double>(LIBC_NAMESPACE::fma(
              static_cast<double>(v[0]), static_cast<double>(v[1]),
              static_cast<double>(v[2])));
        },
        [&] { return LIBC_NAMESPACE::fmal(v[0], v[1], v[2]); });
}

#endif // LIBC_TYPES_LONG_DOUBLE_IS_FLOAT64
