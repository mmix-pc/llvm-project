//===-- Complex transcendental regression tests --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/bit.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/complex_type.h"
#include "src/complex/cacos.h"
#include "src/complex/cacosf.h"
#include "src/complex/cacosh.h"
#include "src/complex/cacoshf.h"
#include "src/complex/casin.h"
#include "src/complex/casinf.h"
#include "src/complex/casinh.h"
#include "src/complex/casinhf.h"
#include "src/complex/catan.h"
#include "src/complex/catanf.h"
#include "src/complex/catanh.h"
#include "src/complex/catanhf.h"
#include "src/complex/ccos.h"
#include "src/complex/ccosf.h"
#include "src/complex/ccosh.h"
#include "src/complex/ccoshf.h"
#include "src/complex/cexp.h"
#include "src/complex/cexpf.h"
#include "src/complex/clog.h"
#include "src/complex/clogf.h"
#include "src/complex/cpow.h"
#include "src/complex/cpowf.h"
#include "src/complex/csin.h"
#include "src/complex/csinf.h"
#include "src/complex/csinh.h"
#include "src/complex/csinhf.h"
#include "src/complex/csqrt.h"
#include "src/complex/csqrtf.h"
#include "src/complex/ctan.h"
#include "src/complex/ctanf.h"
#include "src/complex/ctanh.h"
#include "src/complex/ctanhf.h"
#include "test/UnitTest/FEnvSafeTest.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"
#include "test/UnitTest/TestLogger.h"
#include "utils/MPCWrapper/mpc_inc.h"

namespace {

template <typename T>
class ComplexTranscendentalTest : public LIBC_NAMESPACE::testing::FEnvSafeTest {
  using Bits = LIBC_NAMESPACE::fputil::FPBits<T>;
  using Pair = LIBC_NAMESPACE::Complex<T>;
  using C = LIBC_NAMESPACE::make_complex_t<T>;
  using Func = C (*)(C);

  Pair reference_input{};
  Pair reference_exponent{};
  bool reference_binary = false;

  static void logBits(T value) {
    constexpr unsigned DIGITS = unsigned(sizeof(T) * 2);
    char buffer[DIGITS + 3] = {'0', 'x'};
    auto bits = Bits(value).uintval();
    for (unsigned i = 0; i < DIGITS; ++i)
      buffer[2 + i] = "0123456789abcdef"[(bits >> (4 * (DIGITS - 1 - i))) & 15];
    LIBC_NAMESPACE::testing::tlog << buffer;
  }

  void referenceDiagnostic(const char *component, T got, T expected) {
    using LIBC_NAMESPACE::testing::tlog;
    tlog << "MPC mismatch (" << unsigned(sizeof(T) * 8) << "-bit " << component
         << "): input=(";
    logBits(reference_input.real);
    tlog << ", ";
    logBits(reference_input.imag);
    tlog << ")";
    if (reference_binary) {
      tlog << " exponent=(";
      logBits(reference_exponent.real);
      tlog << ", ";
      logBits(reference_exponent.imag);
      tlog << ")";
    }
    tlog << " got=";
    logBits(got);
    tlog << " expected(RN)=";
    logBits(expected);
    tlog << "\n";
  }

  static C make(T x, T y) {
    return LIBC_NAMESPACE::cpp::bit_cast<C>(Pair{x, y});
  }

  static Pair call(Func f, T x, T y) {
    return LIBC_NAMESPACE::cpp::bit_cast<Pair>(f(make(x, y)));
  }

  void near(T got, T expected) {
    T error = Bits(got - expected).abs().get_val();
    T magnitude = Bits(expected).abs().get_val();
    T tolerance = sizeof(T) == sizeof(float) ? T(0x1p-20) : T(0x1p-49);
    EXPECT_TRUE(error <= magnitude * tolerance);
  }

public:
  // MPCUtils currently dispatches only cabs/carg/cproj despite declaring the
  // other operation enums. Use MPC directly here without changing shared code.
  void referenceComponent(T got, mpfr_srcptr want, double ulps,
                          const char *component) {
    if (mpfr_nan_p(want)) {
      if (!Bits(got).is_nan())
        referenceDiagnostic(component, got, Bits::quiet_nan().get_val());
      EXPECT_TRUE(Bits(got).is_nan());
      return;
    }
    T rounded = sizeof(T) == sizeof(float) ? T(mpfr_get_flt(want, MPFR_RNDN))
                                           : T(mpfr_get_d(want, MPFR_RNDN));
    if (Bits(rounded).is_inf() || rounded == T(0)) {
      if (Bits(rounded).uintval() != Bits(got).uintval())
        referenceDiagnostic(component, got, rounded);
      EXPECT_FP_EQ(rounded, got);
      return;
    }
    if (!Bits(got).is_finite())
      referenceDiagnostic(component, got, rounded);
    EXPECT_TRUE(Bits(got).is_finite());
    if (!Bits(got).is_finite())
      return;
    mpfr_t error, bound;
    mpfr_init2(error, 2048);
    mpfr_init2(bound, 2048);
    mpfr_set_d(error, double(got), MPFR_RNDN);
    mpfr_sub(error, error, want, MPFR_RNDN);
    mpfr_abs(error, error, MPFR_RNDN);
    long exponent = mpfr_get_exp(want) - (sizeof(T) == sizeof(float) ? 24 : 53);
    long minimum = sizeof(T) == sizeof(float) ? -149 : -1074;
    if (exponent < minimum)
      exponent = minimum;
    mpfr_set_d(bound, ulps, MPFR_RNDN);
    mpfr_mul_2si(bound, bound, exponent, MPFR_RNDN);
    bool matches = mpfr_cmp(error, bound) <= 0;
    if (!matches)
      referenceDiagnostic(component, got, rounded);
    EXPECT_TRUE(matches);
    mpfr_clear(error);
    mpfr_clear(bound);
  }

  void allRoundingMPC(Func f, int (*op)(mpc_ptr, mpc_srcptr, mpc_rnd_t)) {
    using LIBC_NAMESPACE::fputil::testing::ForceRoundingMode;
    using LIBC_NAMESPACE::fputil::testing::RoundingMode;
    const RoundingMode modes[] = {RoundingMode::Nearest, RoundingMode::Upward,
                                  RoundingMode::Downward,
                                  RoundingMode::TowardZero};
    const Pair values[] = {
        {T(0.25), T(-0.5)}, {T(-0.25), T(0.5)}, {T(2), T(3)}, {T(-2), T(-3)}};
    mpc_t input, output;
    mpc_init2(input, 2048);
    mpc_init2(output, 2048);
    for (auto mode : modes) {
      ForceRoundingMode rounding(mode);
      ASSERT_TRUE(rounding.success);
      for (Pair z : values) {
        reference_input = z;
        reference_binary = false;
        mpc_set_d_d(input, double(z.real), double(z.imag), MPC_RNDNN);
        op(output, input, MPC_RNDNN);
        auto got = call(f, z.real, z.imag);
        // Check error against the high-precision value, not correct rounding.
        referenceComponent(got.real, mpc_realref(output), 8.0, "real");
        referenceComponent(got.imag, mpc_imagref(output), 8.0, "imaginary");
      }
    }
    mpc_clear(input);
    mpc_clear(output);
  }

  void inverseMPC(Func f, int (*op)(mpc_ptr, mpc_srcptr, mpc_rnd_t)) {
    const T tiny = Bits::min_subnormal().get_val();
    const T max = Bits::max_normal().get_val();
    const T inf = Bits::inf().get_val();
    const T delta = sizeof(T) == sizeof(float) ? T(0x1p-23) : T(0x1p-52);
    const Pair values[] = {
        {T(0), T(-0.0)},  {T(-0.0), T(0)},      {T(0.25), T(-0.5)},
        {T(-1), tiny},    {T(-1), -tiny},       {T(1), tiny},
        {T(1), -tiny},    {T(1) - delta, tiny}, {T(1) + delta, -tiny},
        {tiny, T(1)},     {-tiny, T(1)},        {tiny, T(-1)},
        {-tiny, T(-1)},   {T(2), T(0)},         {T(2), T(-0.0)},
        {T(-2), T(0)},    {T(-2), T(-0.0)},     {max, max},
        {max, tiny},      {-max, -tiny},        {tiny, max},
        {tiny, tiny},     {T(0.75), T(0.75)},   {T(-3), T(4)},
        {T(1), T(0)},     {T(1), T(-0.0)},      {T(-1), T(0)},
        {T(-1), T(-0.0)}, {T(0), T(1)},         {T(-0.0), T(1)},
        {T(0), T(-1)},    {T(-0.0), T(-1)},     {inf, T(1)},
        {-inf, T(-1)},    {T(1), inf},          {T(-1), -inf},
        {inf, inf},       {-inf, inf},          {inf, -inf},
        {-inf, -inf}};
    mpc_t input, output;
    mpc_init2(input, 2048);
    mpc_init2(output, 2048);
    auto check = [&](Pair z) {
      reference_input = z;
      reference_binary = false;
      mpc_set_d_d(input, double(z.real), double(z.imag), MPC_RNDNN);
      op(output, input, MPC_RNDNN);
      auto got = call(f, z.real, z.imag);
      referenceComponent(got.real, mpc_realref(output), 8.0, "real");
      referenceComponent(got.imag, mpc_imagref(output), 8.0, "imaginary");
    };
    for (Pair z : values)
      check(z);
    // Exercise both sides of the real and rotated cuts, including adjacent
    // representable values around the branch points, without random seeds.
    const T coordinates[] = {
        T(-2), T(-1) - delta, T(-1), T(-1) + delta, -tiny, T(-0.0), T(0),
        tiny,  T(1) - delta,  T(1),  T(1) + delta,  T(2)};
    for (T x : coordinates)
      for (T y : coordinates)
        check({x, y});
    mpc_clear(input);
    mpc_clear(output);
  }

  void powerMPC(C (*f)(C, C)) {
    // Zero signs are part of this test's contract, not a global C17 mandate:
    // real-axis results combine the base and exponent zero contributions;
    // half-integral cut values retain their one-sided real-component limit;
    // polynomial cancellation is IEEE +0 in round-to-nearest. In particular,
    // (-2+i0)^(2+i0) = 4+i0, (-4+/-i0)^1.5 has real -0, and (1+i)^2 = +0+2i.
    // Keep bitwise zero comparisons: absolute-error bounds cannot check this.
    struct Input {
      Pair z, w;
    };
    const T tiny = Bits::min_subnormal().get_val();
    const T large = sizeof(T) == sizeof(float) ? T(0x1p60) : T(0x1p300);
    const T small = sizeof(T) == sizeof(float) ? T(0x1p-60) : T(0x1p-300);
    const Input values[] = {
        {{T(-4), T(-0.0)}, {T(0.5), T(0)}},
        {{T(-4), T(0)}, {T(0.5), T(0)}},
        {{T(-2), T(-0.0)}, {T(3), T(0)}},
        {{T(-2), T(0)}, {T(-3), T(0)}},
        {{T(-2), T(0)}, {T(2), T(0)}},
        {{T(-2), T(-0.0)}, {T(2), T(0)}},
        {{T(-4), T(0)}, {T(1.5), T(0)}},
        {{T(-4), T(-0.0)}, {T(1.5), T(0)}},
        {{T(1), T(1)}, {T(2), T(0)}},
        {{T(2), T(3)}, {T(-7), T(0)}},
        {{large, large * T(0.5)}, {T(-2), T(0)}},
        {{small, small * T(0.5)}, {T(-2), T(0)}},
        {{large, -large}, {T(0.5), T(0)}},
        {{small, -small}, {T(0.5), T(0)}},
        {{T(0.5), T(0.25)}, {T(0.3), T(-0.7)}},
        {{T(-0.5), T(-0.25)}, {T(-0.3), T(0.7)}},
        {{T(1), tiny}, {T(2), T(0)}},
        {{T(1), sizeof(T) == sizeof(float) ? T(0x1p-70) : T(0x1p-600)},
         {sizeof(T) == sizeof(float) ? T(0x1p100) : T(0x1p1000), T(0)}},
        {{T(1.25), T(-0.5)}, {T(-1.5), T(-0.5)}}};
    mpc_t input, exponent, output;
    mpc_init2(input, 2048);
    mpc_init2(exponent, 2048);
    mpc_init2(output, 2048);
    for (Input a : values) {
      reference_input = a.z;
      reference_exponent = a.w;
      reference_binary = true;
      mpc_set_d_d(input, double(a.z.real), double(a.z.imag), MPC_RNDNN);
      mpc_set_d_d(exponent, double(a.w.real), double(a.w.imag), MPC_RNDNN);
      mpc_pow(output, input, exponent, MPC_RNDNN);
      auto got = LIBC_NAMESPACE::cpp::bit_cast<Pair>(
          f(make(a.z.real, a.z.imag), make(a.w.real, a.w.imag)));
      referenceComponent(got.real, mpc_realref(output), 16.0, "real");
      referenceComponent(got.imag, mpc_imagref(output), 16.0, "imaginary");
    }
    mpc_clear(input);
    mpc_clear(exponent);
    mpc_clear(output);
  }

  void powerRange(C (*f)(C, C)) {
    const T max = Bits::max_normal().get_val();
    auto evaluate = [&](T x, T y, T u, T v) {
      return LIBC_NAMESPACE::cpp::bit_cast<Pair>(f(make(x, y), make(u, v)));
    };
    // The binary64 phase product max*log(16) overflows, but the output is on
    // the unit circle. This checks range only, not huge-exponent conditioning.
    auto p = evaluate(T(16), T(0), T(0), max);
    EXPECT_TRUE(Bits(p.real).is_finite());
    EXPECT_TRUE(Bits(p.imag).is_finite());
    near(p.real * p.real + p.imag * p.imag, T(1));

    // max is an even integer. Do not approximate max*pi before reduction.
    p = evaluate(T(-1), T(0), max, T(1));
    near(p.real, T(0.04321391826377225));
    EXPECT_TRUE(p.imag == T(0));
    p = evaluate(T(-1), T(-0.0), max, T(1));
    near(p.real, T(23.140692632779267));
    EXPECT_TRUE(p.imag == T(0));
  }

  void squareRoot(Func f) {
    T inf = Bits::inf().get_val(), nan = Bits::quiet_nan().get_val();
    Pair p = call(f, T(-4), T(-0.0));
    EXPECT_FP_EQ(T(0), p.real);
    EXPECT_FP_EQ(T(-2), p.imag);
    p = call(f, T(-0.0), T(-0.0));
    EXPECT_FP_EQ(T(0), p.real);
    EXPECT_FP_EQ(T(-0.0), p.imag);
    p = call(f, T(3), T(4));
    EXPECT_FP_EQ(T(2), p.real);
    EXPECT_FP_EQ(T(1), p.imag);
    p = call(f, nan, -inf);
    EXPECT_FP_EQ(inf, p.real);
    EXPECT_FP_EQ(-inf, p.imag);
    p = call(f, -inf, T(-0.0));
    EXPECT_FP_EQ(T(0), p.real);
    EXPECT_FP_EQ(-inf, p.imag);
    p = call(f, inf, nan);
    EXPECT_FP_EQ(inf, p.real);
    EXPECT_TRUE(Bits(p.imag).is_nan());

    T max = Bits::max_normal().get_val();
    p = call(f, max, max);
    EXPECT_TRUE(Bits(p.real).is_finite() && p.real > T(0));
    EXPECT_TRUE(Bits(p.imag).is_finite() && p.imag > T(0));
    T tiny = Bits::min_subnormal().get_val();
    p = call(f, T(0), tiny);
    T expected =
        sizeof(T) == sizeof(float) ? T(0x1p-75) : T(0x1.6a09e667f3bcdp-538);
    near(p.real, expected);
    near(p.imag, expected);
    // Scaling the input down must not erase a representable small result.
    p = call(f, T(16), tiny * T(8));
    EXPECT_FP_EQ(tiny, p.imag);
  }

  void logarithm(Func f) {
    T inf = Bits::inf().get_val(), nan = Bits::quiet_nan().get_val();
    const T pi = T(0x1.921fb54442d18p+1);
    Pair p = call(f, T(-1), T(-0.0));
    EXPECT_FP_EQ(T(0), p.real);
    EXPECT_FP_EQ(-pi, p.imag);
    p = call(f, T(-0.0), T(0));
    EXPECT_FP_EQ(-inf, p.real);
    EXPECT_FP_EQ(pi, p.imag);
    p = call(f, inf, nan);
    EXPECT_FP_EQ(inf, p.real);
    EXPECT_TRUE(Bits(p.imag).is_nan());
    T max = Bits::max_normal().get_val();
    p = call(f, max, max);
    EXPECT_TRUE(Bits(p.real).is_finite());
    near(p.real, sizeof(T) == sizeof(float) ? T(89.06941267220978)
                                            : T(710.1292864836639));
    EXPECT_FP_EQ(T(pi * T(0.25)), p.imag);
    T y = sizeof(T) == sizeof(float) ? T(0x1p-12) : T(0x1p-30);
    p = call(f, T(1), y);
    near(p.real, T(0.5) * y * y);
    T tiny = Bits::min_subnormal().get_val();
    p = call(f, tiny, -tiny);
    EXPECT_TRUE(Bits(p.real).is_finite() && p.real < T(0));
    EXPECT_FP_EQ(T(-pi * T(0.25)), p.imag);
  }

  void exponential(Func f) {
    T inf = Bits::inf().get_val(), nan = Bits::quiet_nan().get_val();
    Pair p = call(f, T(0), T(-0.0));
    EXPECT_FP_EQ(T(1), p.real);
    EXPECT_FP_EQ(T(-0.0), p.imag);
    p = call(f, inf, T(-0.0));
    EXPECT_FP_EQ(inf, p.real);
    EXPECT_FP_EQ(T(-0.0), p.imag);
    p = call(f, -inf, nan);
    EXPECT_FP_EQ(T(0), p.real);
    EXPECT_TRUE(p.imag == T(0));
    p = call(f, nan, T(-0.0));
    EXPECT_TRUE(Bits(p.real).is_nan());
    EXPECT_FP_EQ(T(-0.0), p.imag);
    p = call(f, T(1), T(1));
    near(p.real, T(1.4686939399158851));
    near(p.imag, T(2.2873552871788423));
    T x = sizeof(T) == sizeof(float) ? T(90) : T(710);
    T y = sizeof(T) == sizeof(float) ? T(0x1p-149f) : T(0x1p-100);
    p = call(f, x, y);
    EXPECT_FP_EQ(inf, p.real);
    EXPECT_TRUE(Bits(p.imag).is_finite() && p.imag > T(0));
    if (sizeof(T) == sizeof(double)) {
      p = call(f, T(1400), T(0x1p-1074));
      EXPECT_FP_EQ(inf, p.real);
      EXPECT_TRUE(Bits(p.imag).is_finite() && p.imag > T(0));
    }
  }

  void hyperbolic(Func sh, Func ch, Func th) {
    T inf = Bits::inf().get_val(), nan = Bits::quiet_nan().get_val();
    Pair p = call(sh, T(-0.0), T(-0.0));
    EXPECT_FP_EQ(T(-0.0), p.real);
    EXPECT_FP_EQ(T(-0.0), p.imag);
    p = call(ch, T(-0.0), T(-0.0));
    EXPECT_FP_EQ(T(1), p.real);
    EXPECT_FP_EQ(T(0), p.imag);
    p = call(th, -inf, nan);
    EXPECT_FP_EQ(T(-1), p.real);
    EXPECT_TRUE(p.imag == T(0));
    p = call(th, T(1000), T(-1));
    EXPECT_FP_EQ(T(1), p.real);
    EXPECT_FP_EQ(T(-0.0), p.imag);
    p = call(th, T(1), T(1));
    near(p.real, T(1.0839233273386945));
    near(p.imag, T(0.2717525853195117));
    p = call(sh, T(1), T(1));
    near(p.real, T(0.6349639147847361));
    near(p.imag, T(1.2984575814159773));
    p = call(ch, T(1), T(1));
    near(p.real, T(0.8337300251311491));
    near(p.imag, T(0.9888977057628651));
    T large = sizeof(T) == sizeof(float) ? T(90) : T(711);
    T tiny = sizeof(T) == sizeof(float) ? T(0x1p-100) : T(0x1p-1000);
    p = call(sh, large, tiny);
    EXPECT_FP_EQ(inf, p.real);
    EXPECT_TRUE(Bits(p.imag).is_finite() && p.imag > T(0));
    p = call(ch, large, tiny);
    EXPECT_FP_EQ(inf, p.real);
    EXPECT_TRUE(Bits(p.imag).is_finite() && p.imag > T(0));
    if (sizeof(T) == sizeof(double)) {
      p = call(th, T(372.75), T(0x1.921fb54442d18p-1));
      EXPECT_FP_EQ(T(1), p.real);
      EXPECT_FP_EQ(T(0x1p-1074), p.imag);
    }
  }

  void trigonometric(Func sn, Func cs, Func tn) {
    Pair p = call(sn, T(-0.0), T(-0.0));
    EXPECT_FP_EQ(T(-0.0), p.real);
    EXPECT_FP_EQ(T(-0.0), p.imag);
    p = call(cs, T(-0.0), T(-0.0));
    EXPECT_FP_EQ(T(1), p.real);
    EXPECT_FP_EQ(T(-0.0), p.imag);
    p = call(tn, T(-0.0), T(-0.0));
    EXPECT_FP_EQ(T(-0.0), p.real);
    EXPECT_FP_EQ(T(-0.0), p.imag);
    p = call(sn, T(1), T(1));
    near(p.real, T(1.2984575814159773));
    near(p.imag, T(0.6349639147847361));
    p = call(cs, T(1), T(1));
    near(p.real, T(0.8337300251311491));
    near(p.imag, T(-0.9888977057628651));
    p = call(tn, T(1), T(1));
    near(p.real, T(0.2717525853195117));
    near(p.imag, T(1.0839233273386945));
    p = call(tn, T(1), T(1000));
    EXPECT_FP_EQ(T(0), p.real);
    EXPECT_FP_EQ(T(1), p.imag);
  }
};

using LlvmLibcComplexDoubleTest = ComplexTranscendentalTest<double>;
using LlvmLibcComplexFloatTest = ComplexTranscendentalTest<float>;

} // namespace

TEST_F(LlvmLibcComplexDoubleTest, SquareRoot) {
  squareRoot(LIBC_NAMESPACE::csqrt);
}

TEST_F(LlvmLibcComplexDoubleTest, Logarithm) {
  logarithm(LIBC_NAMESPACE::clog);
}

TEST_F(LlvmLibcComplexDoubleTest, Exponential) {
  exponential(LIBC_NAMESPACE::cexp);
}

TEST_F(LlvmLibcComplexDoubleTest, Hyperbolic) {
  hyperbolic(LIBC_NAMESPACE::csinh, LIBC_NAMESPACE::ccosh,
             LIBC_NAMESPACE::ctanh);
}

TEST_F(LlvmLibcComplexDoubleTest, Trigonometric) {
  trigonometric(LIBC_NAMESPACE::csin, LIBC_NAMESPACE::ccos,
                LIBC_NAMESPACE::ctan);
}

TEST_F(LlvmLibcComplexDoubleTest, asinMPC) {
  inverseMPC(LIBC_NAMESPACE::casin, mpc_asin);
}

TEST_F(LlvmLibcComplexDoubleTest, acosMPC) {
  inverseMPC(LIBC_NAMESPACE::cacos, mpc_acos);
}

TEST_F(LlvmLibcComplexDoubleTest, atanMPC) {
  inverseMPC(LIBC_NAMESPACE::catan, mpc_atan);
}

TEST_F(LlvmLibcComplexDoubleTest, asinhMPC) {
  inverseMPC(LIBC_NAMESPACE::casinh, mpc_asinh);
}

TEST_F(LlvmLibcComplexDoubleTest, acoshMPC) {
  inverseMPC(LIBC_NAMESPACE::cacosh, mpc_acosh);
}

TEST_F(LlvmLibcComplexDoubleTest, atanhMPC) {
  inverseMPC(LIBC_NAMESPACE::catanh, mpc_atanh);
}

TEST_F(LlvmLibcComplexDoubleTest, PowerMPC) { powerMPC(LIBC_NAMESPACE::cpow); }

TEST_F(LlvmLibcComplexDoubleTest, PowerRange) {
  powerRange(LIBC_NAMESPACE::cpow);
}

TEST_F(LlvmLibcComplexFloatTest, asinMPC) {
  inverseMPC(LIBC_NAMESPACE::casinf, mpc_asin);
}

TEST_F(LlvmLibcComplexFloatTest, acosMPC) {
  inverseMPC(LIBC_NAMESPACE::cacosf, mpc_acos);
}

TEST_F(LlvmLibcComplexFloatTest, atanMPC) {
  inverseMPC(LIBC_NAMESPACE::catanf, mpc_atan);
}

TEST_F(LlvmLibcComplexFloatTest, asinhMPC) {
  inverseMPC(LIBC_NAMESPACE::casinhf, mpc_asinh);
}

TEST_F(LlvmLibcComplexFloatTest, acoshMPC) {
  inverseMPC(LIBC_NAMESPACE::cacoshf, mpc_acosh);
}

TEST_F(LlvmLibcComplexFloatTest, atanhMPC) {
  inverseMPC(LIBC_NAMESPACE::catanhf, mpc_atanh);
}

TEST_F(LlvmLibcComplexFloatTest, PowerMPC) { powerMPC(LIBC_NAMESPACE::cpowf); }

TEST_F(LlvmLibcComplexFloatTest, PowerRange) {
  powerRange(LIBC_NAMESPACE::cpowf);
}

TEST_F(LlvmLibcComplexFloatTest, SquareRoot) {
  squareRoot(LIBC_NAMESPACE::csqrtf);
}

TEST_F(LlvmLibcComplexFloatTest, Logarithm) {
  logarithm(LIBC_NAMESPACE::clogf);
}

TEST_F(LlvmLibcComplexFloatTest, Exponential) {
  exponential(LIBC_NAMESPACE::cexpf);
}

TEST_F(LlvmLibcComplexFloatTest, Hyperbolic) {
  hyperbolic(LIBC_NAMESPACE::csinhf, LIBC_NAMESPACE::ccoshf,
             LIBC_NAMESPACE::ctanhf);
}

TEST_F(LlvmLibcComplexFloatTest, Trigonometric) {
  trigonometric(LIBC_NAMESPACE::csinf, LIBC_NAMESPACE::ccosf,
                LIBC_NAMESPACE::ctanf);
}

#define CHECK_ROUNDING(suffix)                                                 \
  allRoundingMPC(LIBC_NAMESPACE::cacos##suffix, mpc_acos);                     \
  allRoundingMPC(LIBC_NAMESPACE::casin##suffix, mpc_asin);                     \
  allRoundingMPC(LIBC_NAMESPACE::catan##suffix, mpc_atan);                     \
  allRoundingMPC(LIBC_NAMESPACE::cacosh##suffix, mpc_acosh);                   \
  allRoundingMPC(LIBC_NAMESPACE::casinh##suffix, mpc_asinh);                   \
  allRoundingMPC(LIBC_NAMESPACE::catanh##suffix, mpc_atanh);                   \
  allRoundingMPC(LIBC_NAMESPACE::ccos##suffix, mpc_cos);                       \
  allRoundingMPC(LIBC_NAMESPACE::csin##suffix, mpc_sin);                       \
  allRoundingMPC(LIBC_NAMESPACE::ctan##suffix, mpc_tan);                       \
  allRoundingMPC(LIBC_NAMESPACE::ccosh##suffix, mpc_cosh);                     \
  allRoundingMPC(LIBC_NAMESPACE::csinh##suffix, mpc_sinh);                     \
  allRoundingMPC(LIBC_NAMESPACE::ctanh##suffix, mpc_tanh);                     \
  allRoundingMPC(LIBC_NAMESPACE::cexp##suffix, mpc_exp);                       \
  allRoundingMPC(LIBC_NAMESPACE::clog##suffix, mpc_log);                       \
  allRoundingMPC(LIBC_NAMESPACE::csqrt##suffix, mpc_sqrt)

TEST_F(LlvmLibcComplexDoubleTest, FiniteAllRoundingMPC) { CHECK_ROUNDING(); }
TEST_F(LlvmLibcComplexFloatTest, FiniteAllRoundingMPC) { CHECK_ROUNDING(f); }
#undef CHECK_ROUNDING
