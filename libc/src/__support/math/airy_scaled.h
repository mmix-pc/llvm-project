// Adapted from scipy/xsf 665846cc0609721eb8e16168f65dfb867eb9f10f,
// include/xsf/cephes/airy.h (Cephes Math Library Release 2.8, June 2000).
// Copyright 1984, 1987, 1989, 2000 by Stephen L. Moshier
// Copyright (c) 2024, SciPy
// SPDX-License-Identifier: BSD-3-Clause
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_AIRY_SCALED_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_AIRY_SCALED_H

#include "src/__support/FPUtil/double_double.h"
#include "src/__support/math/cos.h"
#include "src/__support/math/exp.h"
#include "src/__support/math/sin.h"
#include "src/__support/math/sqrt.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

struct ScaledAiry {
  double ai;
  double ai_prime;
  double bi;
  double bi_prime;
};

namespace airy_internal {

template <unsigned N>
LIBC_INLINE double polynomial(double x, const double (&coefficients)[N]) {
  double value = coefficients[0];
  for (unsigned i = 1; i < N; ++i)
    value = value * x + coefficients[i];
  return value;
}

template <unsigned N>
LIBC_INLINE double monic_polynomial(double x, const double (&coefficients)[N]) {
  double value = x + coefficients[0];
  for (unsigned i = 1; i < N; ++i)
    value = value * x + coefficients[i];
  return value;
}

inline constexpr double AI_NUM[] = {
    3.46538101525629032477E-1, 1.20075952739645805542E1,
    7.62796053615234516538E1,  1.68089224934630576269E2,
    1.59756391350164413639E2,  7.05360906840444183113E1,
    1.40264691163389668864E1,  9.99999999999999995305E-1};
inline constexpr double AI_DEN[] = {
    5.67594532638770212846E-1, 1.47562562584847203173E1,
    8.45138970141474626562E1,  1.77318088145400459522E2,
    1.64234692871529701831E2,  7.14778400825575695274E1,
    1.40959135607834029598E1,  1.00000000000000000470E0};
inline constexpr double AIP_NUM[] = {
    6.13759184814035759225E-1, 1.47454670787755323881E1,
    8.20584123476060982430E1,  1.71184781360976385540E2,
    1.59317847137141783523E2,  6.99778599330103016170E1,
    1.39470856980481566958E1,  1.00000000000000000550E0};
inline constexpr double AIP_DEN[] = {
    3.34203677749736953049E-1, 1.11810297306158156705E1,
    7.11727352147859965283E1,  1.58778084372838313640E2,
    1.53206427475809220834E2,  6.86752304592780337944E1,
    1.38498634758259442477E1,  9.99999999999999994502E-1};
inline constexpr double BI_NUM[] = {
    -2.53240795869364152689E-1, 5.75285167332467384228E-1,
    -3.29907036873225371650E-1, 6.44404068948199951727E-2,
    -3.82519546641336734394E-3};
inline constexpr double BI_DEN[] = {
    -7.15685095054035237902E0, 1.06039580715664694291E1,
    -5.23246636471251500874E0, 9.57395864378383833152E-1,
    -5.50828147163549611107E-2};
inline constexpr double BIP_NUM[] = {
    4.65461162774651610328E-1, -1.08992173800493920734E0,
    6.38800117371827987759E-1, -1.26844349553102907034E-1,
    7.62487844342109852105E-3};
inline constexpr double BIP_DEN[] = {
    -8.70622787633159124240E0, 1.38993162704553213172E1,
    -7.14116144616431159572E0, 1.34008595960680518666E0,
    -7.84273211323341930448E-2};

inline constexpr double NEG_AFN[] = {
    -1.31696323418331795333E-1, -6.26456544431912369773E-1,
    -6.93158036036933542233E-1, -2.79779981545119124951E-1,
    -4.91900132609500318020E-2, -4.06265923594885404393E-3,
    -1.59276496239262096340E-4, -2.77649108155232920844E-6,
    -1.67787698489114633780E-8,
};

inline constexpr double NEG_AFD[] = {
    /* 1.00000000000000000000E0, */
    1.33560420706553243746E1,  3.26825032795224613948E1,
    2.67367040941499554804E1,  9.18707402907259625840E0,
    1.47529146771666414581E0,  1.15687173795188044134E-1,
    4.40291641615211203805E-3, 7.54720348287414296618E-5,
    4.51850092970580378464E-7,
};

inline constexpr double NEG_AGN[] = {
    1.97339932091685679179E-2,  3.91103029615688277255E-1,
    1.06579897599595591108E0,   9.39169229816650230044E-1,
    3.51465656105547619242E-1,  6.33888919628925490927E-2,
    5.85804113048388458567E-3,  2.82851600836737019778E-4,
    6.98793669997260967291E-6,  8.11789239554389293311E-8,
    3.41551784765923618484E-10,
};

inline constexpr double NEG_AGD[] = {
    /*  1.00000000000000000000E0, */
    9.30892908077441974853E0,  1.98352928718312140417E1,
    1.55646628932864612953E1,  5.47686069422975497931E0,
    9.54293611618961883998E-1, 8.64580826352392193095E-2,
    4.12656523824222607191E-3, 1.01259085116509135510E-4,
    1.17166733214413521882E-6, 4.91834570062930015649E-9,
};

inline constexpr double NEG_APFN[] = {
    1.85365624022535566142E-1, 8.86712188052584095637E-1,
    9.87391981747398547272E-1, 4.01241082318003734092E-1,
    7.10304926289631174579E-2, 5.90618657995661810071E-3,
    2.33051409401776799569E-4, 4.08718778289035454598E-6,
    2.48379932900442457853E-8,
};

inline constexpr double NEG_APFD[] = {
    /*  1.00000000000000000000E0, */
    1.47345854687502542552E1,  3.75423933435489594466E1,
    3.14657751203046424330E1,  1.09969125207298778536E1,
    1.78885054766999417817E0,  1.41733275753662636873E-1,
    5.44066067017226003627E-3, 9.39421290654511171663E-5,
    5.65978713036027009243E-7,
};

inline constexpr double NEG_APGN[] = {
    -3.55615429033082288335E-2,  -6.37311518129435504426E-1,
    -1.70856738884312371053E0,   -1.50221872117316635393E0,
    -5.63606665822102676611E-1,  -1.02101031120216891789E-1,
    -9.48396695961445269093E-3,  -4.60325307486780994357E-4,
    -1.14300836484517375919E-5,  -1.33415518685547420648E-7,
    -5.63803833958893494476E-10,
};

inline constexpr double NEG_APGD[] = {
    /*  1.00000000000000000000E0, */
    9.85865801696130355144E0,  2.16401867356585941885E1,
    1.73130776389749389525E1,  6.17872175280828766327E0,
    1.08848694396321495475E0,  9.95005543440888479402E-2,
    4.78468199683886610842E-3, 1.18159633322838625562E-4,
    1.37480673554219441465E-6, 5.79912514929147598821E-9,
};

} // namespace airy_internal

// Internal nonnegative-argument kernel: 0 <= x <= 2^32, round-to-nearest.
// Ai and Ai' are multiplied by exp(zeta); Bi and Bi' by exp(-zeta), where
// zeta = 2*x^(3/2)/3. No exponential is evaluated in the large-x branch.
LIBC_INLINE ScaledAiry airy_scaled_positive(double x) {
  using namespace airy_internal;
  constexpr double INV_SQRT_PI = 0.564189583547756286948;
  constexpr double C1 = 0.35502805388781723926;
  constexpr double C2 = 0.258819403792806798405;
  constexpr double SQRT3 = 1.73205080756887729353;
  if (x < 0x1p-54)
    return {C1, -C2, SQRT3 * C1, SQRT3 * C2};
  double root = math::sqrt(x);
  double zeta = (2.0 / 3.0) * x * root;
  ScaledAiry result{};
  if (x >= 2.09) {
    double fourth_root = math::sqrt(root);
    double z = 1.0 / zeta;
    result.ai = (0.5 * INV_SQRT_PI / fourth_root) *
                (polynomial(z, AI_NUM) / polynomial(z, AI_DEN));
    result.ai_prime = (-0.5 * INV_SQRT_PI * fourth_root) *
                      (polynomial(z, AIP_NUM) / polynomial(z, AIP_DEN));
    if (x > 8.3203353) {
      result.bi =
          (INV_SQRT_PI / fourth_root) *
          (1.0 + z * polynomial(z, BI_NUM) / monic_polynomial(z, BI_DEN));
      result.bi_prime =
          (INV_SQRT_PI * fourth_root) *
          (1.0 + z * polynomial(z, BIP_NUM) / monic_polynomial(z, BIP_DEN));
      return result;
    }
  }

  // Entire ODE basis F(0)=1, F'(0)=0; G(0)=0, G'(0)=1. At this bounded
  // argument 64 terms put all four tails below 2^-206; this is not a work cap
  // returning an unconverged approximation for an unbounded input.
  double cube = x * x * x;
  double f = 1.0, g = x, fp = 0.0, gp = 1.0;
  double ft = 1.0, gt = x;
  double fpt = x * x / 2.0, gpt = cube / 3.0;
  for (unsigned k = 1; k <= 64; ++k) {
    double m = 3.0 * k;
    ft *= cube / (m * (m - 1.0));
    gt *= cube / (m * (m + 1.0));
    f += ft;
    g += gt;
    fp += fpt;
    gp += gpt;
    fpt *= cube / ((m + 2.0) * m);
    gpt *= cube / ((m + 3.0) * (m + 1.0));
    if (ft < 0x1p-60 && gt < 0x1p-60 && fpt < 0x1p-60 && gpt < 0x1p-60)
      break;
  }
  double scale = math::exp(zeta);
  if (x < 2.09) {
    result.ai = (C1 * f - C2 * g) * scale;
    result.ai_prime = (C1 * fp - C2 * gp) * scale;
  }
  result.bi = SQRT3 * (C1 * f + C2 * g) / scale;
  result.bi_prime = SQRT3 * (C1 * fp + C2 * gp) / scale;
  return result;
}

// Negative-axis amplitude/corrections with an independently reduced phase.
// a > 2.09; s/c are sin/cos(2*a^(3/2)/3), inverse_phase is its reciprocal.
LIBC_INLINE ScaledAiry airy_negative_asymptotic(double a, double inverse_phase,
                                                double s, double c) {
  using namespace airy_internal;
  constexpr double INV_SQRT2 = 0.707106781186547524401;
  double f = (s + c) * INV_SQRT2;
  double g = (c - s) * INV_SQRT2;
  double z = inverse_phase, zz = z * z;
  double uf =
      1.0 + zz * polynomial(zz, NEG_AFN) / monic_polynomial(zz, NEG_AFD);
  double ug = z * polynomial(zz, NEG_AGN) / monic_polynomial(zz, NEG_AGD);
  double fourth_root = math::sqrt(math::sqrt(a));
  constexpr double INV_SQRT_PI = 0.564189583547756286948;
  double k = INV_SQRT_PI / fourth_root;
  ScaledAiry result{k * (f * uf - g * ug), 0.0, k * (g * uf + f * ug), 0.0};
  uf = 1.0 + zz * polynomial(zz, NEG_APFN) / monic_polynomial(zz, NEG_APFD);
  ug = z * polynomial(zz, NEG_APGN) / monic_polynomial(zz, NEG_APGD);
  k = INV_SQRT_PI * fourth_root;
  result.ai_prime = -k * (g * uf + f * ug);
  result.bi_prime = k * (f * uf - g * ug);
  return result;
}

// Ai, Ai', Bi, Bi' without exponential scaling for -2^32 <= x <= 0, RN.
LIBC_INLINE ScaledAiry airy_negative(double x) {
  using namespace airy_internal;
  constexpr double C1 = 0.35502805388781723926;
  constexpr double C2 = 0.258819403792806798405;
  constexpr double SQRT3 = 1.73205080756887729353;
  double a = -x;
  if (a < 0x1p-54)
    return {C1, -C2, SQRT3 * C1, SQRT3 * C2};
  if (a <= 2.09) {
    double cube = x * x * x;
    double f = 1.0, g = x, fp = 0.0, gp = 1.0;
    double ft = 1.0, gt = x;
    double fpt = x * x / 2.0, gpt = cube / 3.0;
    // The absolute series is bounded by the positive kernel's 64-term tail.
    for (unsigned k = 1; k <= 64; ++k) {
      double m = 3.0 * k;
      ft *= cube / (m * (m - 1.0));
      gt *= cube / (m * (m + 1.0));
      f += ft;
      g += gt;
      fp += fpt;
      gp += gpt;
      fpt *= cube / ((m + 2.0) * m);
      gpt *= cube / ((m + 3.0) * (m + 1.0));
      auto small = [](double t) { return t > -0x1p-60 && t < 0x1p-60; };
      if (small(ft) && small(gt) && small(fpt) && small(gpt))
        break;
    }
    return {C1 * f - C2 * g, C1 * fp - C2 * gp, SQRT3 * (C1 * f + C2 * g),
            SQRT3 * (C1 * fp + C2 * gp)};
  }

  // Recover the square-root residual and form 2*a*sqrt(a)/3 in two parts.
  // A single rounded phase would lose the oscillation at large arguments.
  double root = math::sqrt(a);
  auto square = fputil::exact_mult(root, root);
  double root_low = ((a - square.hi) - square.lo) / (2.0 * root);
  auto product = fputil::quick_mult(a, fputil::DoubleDouble{root_low, root});
  auto phase = fputil::div(product, fputil::DoubleDouble{0.0, 1.5});
  phase = fputil::exact_add(phase.hi, phase.lo);
  double sh = math::sin(phase.hi), ch = math::cos(phase.hi);
  double sl = math::sin(phase.lo), cl = math::cos(phase.lo);
  double s = sh * cl + ch * sl;
  double c = ch * cl - sh * sl;
  return airy_negative_asymptotic(a, 1.0 / phase.hi, s, c);
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_AIRY_SCALED_H
