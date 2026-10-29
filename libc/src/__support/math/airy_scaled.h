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

#include "src/__support/math/exp.h"
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

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_AIRY_SCALED_H
