//===-- Strict gamma recurrence regressions -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/math/tgamma.h"
#include "src/math/tgammaf.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/RoundingModeUtils.h"
#include "test/UnitTest/Test.h"
#include "utils/MPFRWrapper/mpfr_inc.h"

using LIBC_NAMESPACE::fputil::testing::ForceRoundingMode;
using LIBC_NAMESPACE::fputil::testing::RoundingMode;

class LlvmLibcGammaRecurrenceTest
    : public LIBC_NAMESPACE::testing::FPTest<double> {
public:
  void checkRounded(double x, bool single = false) {
    struct Mode {
      RoundingMode hardware;
      mpfr_rnd_t reference;
    };
    const Mode modes[] = {
        {RoundingMode::Nearest, MPFR_RNDN},
        {RoundingMode::Upward, MPFR_RNDU},
        {RoundingMode::Downward, MPFR_RNDD},
        {RoundingMode::TowardZero, MPFR_RNDZ},
    };
    mpfr_t input, reference;
    // The Euler-constant correction to 1/x must survive even at x=2^-1022:
    // a 384-bit oracle would lose it and give wrong directed-rounding answers.
    mpfr_inits2(2048, input, reference, (mpfr_ptr)0);
    mpfr_set_d(input, x, MPFR_RNDN);
    mpfr_gamma(reference, input, MPFR_RNDN);
    for (const Mode &mode : modes) {
      ForceRoundingMode rounding(mode.hardware);
      if (!rounding.success)
        continue;
      if (single) {
        float expected = mpfr_get_flt(reference, mode.reference);
        EXPECT_FP_EQ(expected, LIBC_NAMESPACE::tgammaf(static_cast<float>(x)));
      } else {
        double expected = mpfr_get_d(reference, mode.reference);
        EXPECT_FP_EQ(expected, LIBC_NAMESPACE::tgamma(x));
      }
    }
    mpfr_clears(input, reference, (mpfr_ptr)0);
  }
};

TEST_F(LlvmLibcGammaRecurrenceTest, NativeFailureInputs) {
  // Regression inputs for the exp(lgamma(x)) accuracy loss. Require the MPFR
  // value, rather than a relative tolerance that can conceal hundreds of ulps.
  const double inputs[] = {
      -0x1.0c34b3e01e6e7p+3,
      -0x1.a206f0a19dcc4p+2,
      0x1.288bbb0d6a1e6p+3,
      0x1p+6,
      0x1p+7,
      0x1.14p+7,
      0x1.16p+7,
      0x1.18p+7,
      0x1.1ap+7,
      0x1.1cp+7,
      0x1.1ep+7,
      0x1.2p+7,
      0x1.22p+7,
      0x1.52p+7,
      0x1.54p+7,
      0x1.56p+7,
      0x0.4000000000004p-1022,
      -0x0.4000000000004p-1022,
      -0x1.ffffffffffffcp-1,
      -0x1.ffffffffffffdp-1,
      -0x1.ffffffffffffep-1,
      -0x1.fffffffffffffp-1,
      -0x1.0000000000001p+0,
      -0x1.0000000000002p+0,
      -0x1.0000000000004p+0,
      -0x1.ffffffffffffcp+0,
      -0x1.ffffffffffffdp+0,
      -0x1.ffffffffffffep+0,
      -0x1.fffffffffffffp+0,
      -0x1.0000000000001p+1,
      -0x1.0000000000002p+1,
      -0x1.0000000000003p+1,
      -0x1.0000000000004p+1,
      -0x1.7fffffffffffdp+1,
      -0x1.7fffffffffffep+1,
      -0x1.7ffffffffffffp+1,
      -0x1.5800000000001p+7,
      -0x1.5800000000002p+7,
      -0x1.5800000000003p+7,
      -0x1.5800000000004p+7,
      -0x1.59ffffffffffcp+7,
      -0x1.59ffffffffffdp+7,
      -0x1.59ffffffffffep+7,
      -0x1.59fffffffffffp+7,
      -0x1.5a00000000001p+7,
      -0x1.5bfffffffffffp+7,
      -0x1.5c00000000001p+7,
      -0x1.5dfffffffffffp+7,
      -0x1.5e00000000001p+7,
      -0x1.5ffffffffffffp+7,
      -0x1.6000000000001p+7,
      -0x1.61fffffffffffp+7,
      -0x1.6200000000001p+7,
      -0x1.cp+1,
      -0x1.6p+2,
      -0x1.ap+2,
      -0x1.1p+3,
      -0x1.3p+3,
      -0x1.5p+3,
      -0x1.7p+3,
      -0x1.9p+3,
      -0x1.dp+3,
      -0x1.08p+4,
      -0x1.04p+5,
      -0x1.02p+6,
      -0x1.01p+7,
      -0x1.4fp+7,
      -0x1.51p+7,
      -0x1.53p+7,
      -0x1.55p+7,
      -0x1.facae9c9e7105p+5,
      -0x1.fdce012aee935p+6,
      0x1.1a781948507bcp+7,
      -0x1.1a781948507bcp+7,
      0x1.57p+7,
      0x1.573fae561f646p+7,
      0x1.573fae561f647p+7,
      0x1.edd3c07ee0b0bp+6,
      -0x1.edd3c07ee0b0bp+6,
      0x1.8b0fcd324d5a2p+3,
      -0x1.8b0fcd324d5a2p+3,
  };
  for (double x : inputs)
    checkRounded(x);
}

TEST_F(LlvmLibcGammaRecurrenceTest, RecurrenceAndFloatRounding) {
  for (int i = -799; i < 688; ++i) {
    double x = static_cast<double>(i) * 0.25 + 0.125;
    checkRounded(x);
    checkRounded(x, true);
  }
  const double boundaries[] = {0x1p-1074,
                               -0x1p-1074,
                               0x1p-1022,
                               -0x1p-1022,
                               0x1.fffffffffffffp-71,
                               -0x1.fffffffffffffp-71,
                               0x1p-70,
                               -0x1p-70,
                               0x1.0000000000001p-70,
                               -0x1.0000000000001p-70,
                               0x1.fffffffffffffp-2,
                               0x1p-1,
                               0x1.0000000000001p-1,
                               0x1.7ffffffffffffp0,
                               0x1.8p0,
                               0x1.8000000000001p0,
                               1.0,
                               2.0,
                               23.0,
                               24.0,
                               36.0,
                               37.0,
                               64.0,
                               128.0,
                               171.0,
                               172.0,
                               0x1.573fae561f647p7,
                               0x1.573fae561f648p7,
                               -200.5,
                               -201.5};
  for (double x : boundaries)
    checkRounded(x);
  const float float_boundaries[] = {
      0x1p-149f,      -0x1p-149f, 0x1p-126f,    -0x1p-126f,    -0x1.fffffep-1f,
      -0x1.000002p0f, 35.0f,      0x1.18522p5f, 0x1.185222p5f, 36.0f};
  for (float x : float_boundaries)
    checkRounded(static_cast<double>(x), true);
}

TEST_F(LlvmLibcGammaRecurrenceTest, TinyInputDoesNotRaiseUnderflow) {
  ForceRoundingMode rounding(RoundingMode::Nearest);
  if (!rounding.success)
    return;
  libc_errno = 0;
  EXPECT_FP_EQ_WITH_EXCEPTION(0x1.fffffffffffep1023,
                              LIBC_NAMESPACE::tgamma(0x0.4000000000004p-1022),
                              FE_INEXACT);
  EXPECT_MATH_ERRNO(0);
  EXPECT_FP_EQ_WITH_EXCEPTION(-0x1.fffffffffffep1023,
                              LIBC_NAMESPACE::tgamma(-0x0.4000000000004p-1022),
                              FE_INEXACT);
  EXPECT_MATH_ERRNO(0);
}
