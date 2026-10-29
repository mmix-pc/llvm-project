//===-- Tests for the large-argument Bessel kernel
//-------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/FPUtil/FPBits.h"
#include "src/__support/libc_errno.h"
#include "src/__support/math/bessel_hankel.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"
#include <mpfr.h>

TEST(LlvmLibcBesselHankelTest, IndependentReference) {
  using Bits = LIBC_NAMESPACE::fputil::FPBits<double>;
  LIBC_NAMESPACE::fputil::testing::ForceRoundingMode guard(
      LIBC_NAMESPACE::fputil::testing::RoundingMode::Nearest);
  ASSERT_TRUE(guard.success);
  mpfr_t input, low, high, actual, error, limit;
  mpfr_init2(low, 256);
  mpfr_inits2(512, input, high, actual, error, limit, (mpfr_ptr)0);
  const unsigned orders[] = {0, 1, 2, 3, 17, 2048, 0x7fffffff, 0x80000000};
  for (unsigned n : orders) {
    double lower = 16.0 * double(n) * double(n);
    if (lower < 128.0)
      lower = 128.0;
    const double points[] = {lower, Bits(Bits(lower).uintval() + 1).get_val(),
                             lower * 2.0, 0x1p100,
                             Bits::max_normal().get_val()};
    for (double x : points) {
      libc_errno = 0;
      LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
      auto result = LIBC_NAMESPACE::math::bessel_hankel(n, x);
      EXPECT_EQ(int(libc_errno), 0);
      EXPECT_EQ(LIBC_NAMESPACE::fputil::test_except(
                    FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW),
                0);
      const double results[] = {result.j, result.y};
      mpfr_set_d(input, x, MPFR_RNDN);
      mpfr_sqrt(limit, input, MPFR_RNDN);
      mpfr_ui_div(limit, 1, limit, MPFR_RNDN);
      mpfr_mul_2si(limit, limit, -45, MPFR_RNDN);
      for (unsigned kind = 0; kind < 2; ++kind) {
        auto function = kind ? mpfr_yn : mpfr_jn;
        function(low, static_cast<long>(n), input, MPFR_RNDN);
        function(high, static_cast<long>(n), input, MPFR_RNDN);
        ASSERT_EQ(Bits(mpfr_get_d(low, MPFR_RNDN)).uintval(),
                  Bits(mpfr_get_d(high, MPFR_RNDN)).uintval());
        mpfr_set_d(actual, results[kind], MPFR_RNDN);
        mpfr_sub(error, actual, high, MPFR_RNDN);
        mpfr_abs(error, error, MPFR_RNDN);
        EXPECT_TRUE(mpfr_number_p(error) && mpfr_cmp(error, limit) <= 0);
      }
    }
  }
  mpfr_clears(input, low, high, actual, error, limit, (mpfr_ptr)0);
}
