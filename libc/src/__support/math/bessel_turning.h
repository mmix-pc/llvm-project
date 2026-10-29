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

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_TURNING_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_TURNING_H

#include "src/__support/math/airy_scaled.h"
#include "src/__support/math/cbrt.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

struct BesselPair {
  double j;
  double y;
};

// Internal turning-region approximation, RN: integral 2048 <= n <= 2^31,
// |x-n|/cbrt(n) <= 0.7. Public dispatch and error handling are separate.
LIBC_INLINE BesselPair bessel_turning(double n, double x) {
  constexpr double PF2[] = {-9.0000000000000000000e-2,
                            8.5714285714285714286e-2};
  constexpr double PF3[] = {1.3671428571428571429e-1, -5.4920634920634920635e-2,
                            -4.4444444444444444444e-3};
  constexpr double PF4[] = {1.3500000000000000000e-3, -1.6036054421768707483e-1,
                            4.2590187590187590188e-2, 2.7330447330447330447e-3};
  constexpr double PG1[] = {-2.4285714285714285714e-1,
                            1.4285714285714285714e-2};
  constexpr double PG2[] = {-9.0000000000000000000e-3, 1.9396825396825396825e-1,
                            -1.1746031746031746032e-2};
  constexpr double PG3[] = {1.9607142857142857143e-2, -1.5983694083694083694e-1,
                            6.3838383838383838384e-3};
  using airy_internal::polynomial;
  double cbn = math::cbrt(n);
  double z = (x - n) / cbn;
  constexpr double CB2 = 1.259921049894873164767;
  constexpr double CB4 = 1.587401051968199474752;
  double argument = -CB2 * z;
  ScaledAiry airy;
  if (argument <= 0) {
    airy = airy_negative(argument);
  } else {
    airy = airy_scaled_positive(argument);
    double scale = math::exp((2.0 / 3.0) * argument * math::sqrt(argument));
    airy.ai /= scale;
    airy.ai_prime /= scale;
    airy.bi *= scale;
    airy.bi_prime *= scale;
  }
  double zz = z * z, cube = zz * z;
  double f[] = {1.0, -z / 5.0, polynomial(cube, PF2) * zz,
                polynomial(cube, PF3), polynomial(cube, PF4) * z};
  double g[] = {0.3 * zz, polynomial(cube, PG1), polynomial(cube, PG2) * z,
                polynomial(cube, PG3) * zz};
  double pp = 0.0, qq = 0.0, power = 1.0;
  double n23 = cbn * cbn;
  for (unsigned k = 0; k < 5; ++k) {
    pp += f[k] * power;
    if (k < 4)
      qq += g[k] * power;
    power /= n23;
  }
  return {CB2 * airy.ai * pp / cbn + CB4 * airy.ai_prime * qq / n,
          -CB2 * airy.bi * pp / cbn - CB4 * airy.bi_prime * qq / n};
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_BESSEL_TURNING_H
