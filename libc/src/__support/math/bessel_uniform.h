// Adapted from scipy/xsf 665846cc0609721eb8e16168f65dfb867eb9f10f,
// include/xsf/cephes/jv.h (Cephes Math Library Release 2.8, June 2000).
// Copyright 1984, 1987, 1989, 1992, 2000 by Stephen L. Moshier
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

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_UNIFORM_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_UNIFORM_H

#include "src/__support/math/bessel_phase.h"
#include "src/__support/math/bessel_turning.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {
namespace bessel_uniform_internal {

constexpr double lambda[] = {1.0,
                             1.041666666666666666666667E-1,
                             8.355034722222222222222222E-2,
                             1.282265745563271604938272E-1,
                             2.918490264641404642489712E-1,
                             8.816272674437576524187671E-1,
                             3.321408281862767544702647E+0,
                             1.499576298686255465867237E+1,
                             7.892301301158651813848139E+1,
                             4.744515388682643231611949E+2,
                             3.207490090890661934704328E+3};

constexpr double mu[] = {1.0,
                         -1.458333333333333333333333E-1,
                         -9.874131944444444444444444E-2,
                         -1.433120539158950617283951E-1,
                         -3.172272026784135480967078E-1,
                         -9.424291479571202491373028E-1,
                         -3.511203040826354261542798E+0,
                         -1.572726362036804512982712E+1,
                         -8.228143909718594444224656E+1,
                         -4.923553705236705240352022E+2,
                         -3.316218568547972508762102E+3};

constexpr double P1[] = {-2.083333333333333333333333E-1,
                         1.250000000000000000000000E-1};

constexpr double P2[] = {3.342013888888888888888889E-1,
                         -4.010416666666666666666667E-1,
                         7.031250000000000000000000E-2};

constexpr double P3[] = {
    -1.025812596450617283950617E+0, 1.846462673611111111111111E+0,
    -8.912109375000000000000000E-1, 7.324218750000000000000000E-2};

constexpr double P4[] = {
    4.669584423426247427983539E+0, -1.120700261622299382716049E+1,
    8.789123535156250000000000E+0, -2.364086914062500000000000E+0,
    1.121520996093750000000000E-1};

constexpr double P5[] = {-2.8212072558200244877E1, 8.4636217674600734632E1,
                         -9.1818241543240017361E1, 4.2534998745388454861E1,
                         -7.3687943594796316964E0, 2.27108001708984375E-1};

constexpr double P6[] = {2.1257013003921712286E2, -7.6525246814118164230E2,
                         1.0599904525279998779E3, -6.9957962737613254123E2,
                         2.1819051174421159048E2, -2.6491430486951555525E1,
                         5.7250142097473144531E-1};

constexpr double P7[] = {-1.9194576623184069963E3, 8.0617221817373093845E3,
                         -1.3586550006434137439E4, 1.1655393336864533248E4,
                         -5.3056469786134031084E3, 1.2009029132163524628E3,
                         -1.0809091978839465550E2, 1.7277275025844573975E0};

LIBC_INLINE ScaledAiry oscillatory_airy(double a,
                                        bessel_phase_internal::Wide phase) {
  using bessel_phase_internal::Wide;
  if (a <= 2.09)
    return airy_negative(-a);
  double inverse_phase = double(fputil::approx_reciprocal(phase));
  double s = 0.0, c = 1.0;
  // Three binary64 pieces preserve the 128-bit phase through 2^67.
  for (unsigned k = 0; k < 3; ++k) {
    double part = double(phase);
    double sp = math::sin(part), cp = math::cos(part);
    double next_s = s * cp + c * sp;
    c = c * cp - s * sp;
    s = next_s;
    phase = fputil::quick_sub(phase, Wide(part));
  }
  return airy_negative_asymptotic(a, inverse_phase, s, c);
}

} // namespace bessel_uniform_internal

struct UniformBesselPair {
  double j;
  double y;
  bessel_phase_internal::Wide phase;
  bool scaled;
};

// Internal RN kernel: 2048 <= n <= 2^31, finite 0 < x <= 16*n*n.
// Nonoscillatory results outside the turning branch keep exp(-phase) for J
// and exp(+phase) for Y separate; public range handling restores them.
LIBC_INLINE UniformBesselPair bessel_uniform(double n, double x) {
  using namespace bessel_uniform_internal;
  using airy_internal::polynomial;
  using bessel_phase_internal::Wide;
  double cbn = math::cbrt(n);
  double local = (x - n) / cbn;
  if (local >= -0.7 && local <= 0.7) {
    auto result = bessel_turning(n, x);
    return {result.j, result.y, Wide(0.0), false};
  }
  auto geometry = bessel_phase(n, x);
  double zz = geometry.square;
  bool below = x < n;
  double sz = math::sqrt(below ? zz : -zz);
  double t =
      1.5 * double(bessel_phase_internal::divide(geometry.phase, Wide(n)));
  double z32i = 1.0 / t;
  double sqz = math::cbrt(t);
  double n23 = cbn * cbn;
  double airy_argument = n23 * (below ? geometry.zeta : -geometry.zeta);
  ScaledAiry airy = below ? airy_scaled_positive(airy_argument)
                          : oscillatory_airy(airy_argument, geometry.phase);

  double zzi = 1.0 / zz;
  double square = zz * zz;
  double cube = square * zz;
  double u[] = {1.0,
                polynomial(zzi, P1) / sz,
                polynomial(zzi, P2) / zz,
                polynomial(zzi, P3) / (sz * zz),
                polynomial(zzi, P4) / square,
                polynomial(zzi, P5) / (square * sz),
                polynomial(zzi, P6) / cube,
                polynomial(zzi, P7) / (cube * sz)};
  double pp = 0.0, qq = 0.0, np = 1.0;
  double previous_a = 0x1.fffffffffffffp1023;
  double previous_b = previous_a;
  bool use_a = true, use_b = true;
  int region_sign = below ? 1 : -1;
  for (unsigned k = 0; k <= 3; ++k) {
    unsigned even = 2 * k, odd = even + 1;
    double zp = 1.0, ak = 0.0, bk = 0.0;
    for (unsigned s = 0; s <= even; ++s) {
      int sign_a = (s & 3) > 1 ? region_sign : 1;
      unsigned m = odd - s;
      int sign_b = ((m + 1) & 3) > 1 ? region_sign : 1;
      ak += double(sign_a) * mu[s] * zp * u[even - s];
      bk += double(sign_b) * lambda[s] * zp * u[m];
      zp *= z32i;
    }
    bk += lambda[odd] * zp;
    ak *= np;
    bk *= -np / sqz;
    double magnitude_a = ak < 0 ? -ak : ak;
    double magnitude_b = bk < 0 ? -bk : bk;
    use_a = use_a && magnitude_a < previous_a;
    use_b = use_b && magnitude_b < previous_b;
    if (use_a) {
      pp += ak;
      previous_a = magnitude_a;
    }
    if (use_b) {
      qq += bk;
      previous_b = magnitude_b;
    }
    np /= n * n;
  }
  return {geometry.amplitude *
              (airy.ai * pp / cbn + airy.ai_prime * qq / (n23 * n)),
          -geometry.amplitude *
              (airy.bi * pp / cbn + airy.bi_prime * qq / (n23 * n)),
          geometry.phase, below};
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_UNIFORM_H
