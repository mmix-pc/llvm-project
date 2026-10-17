//===-- Unittests for atan2 -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/FPUtil/FPBits.h"
#include "src/math/atan2.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"
#include "utils/MPFRWrapper/MPFRUtils.h"
#include "utils/MPFRWrapper/mpfr_inc.h"

using LlvmLibcAtan2Test = LIBC_NAMESPACE::testing::FPTest<double>;
using LIBC_NAMESPACE::testing::tlog;

namespace mpfr = LIBC_NAMESPACE::testing::mpfr;

TEST_F(LlvmLibcAtan2Test, ExtremeRatios) {
  const mpfr::BinaryInput<double> inputs[] = {
      {0x1p-1074, 0x1.fffffffffffffp1023},
      {0x1p-1022, 0x1.fffffffffffffp1023},
      {0x1.fffffffffffffp1023, 0x1p-1022},
      {0x1p-1023, 1.0},
      {0x1p-1022, 1.0},
      {0x1p-1074, 0x1p-1074},
      {0x1p-1074, 0x1p-970},
      {0x1p-970, 0x1p-1074},
      {0x1.fffffffffffffp1023, 0x1.fffffffffffffp1023},
      {0x1.fffffffffffffp-55, 1.0},
      {0x1p-55, 0x1.0000000000001p0},
      {0x1.fffffffffffffp-65, 1.0},
      {0x1p-65, 0x1.0000000000001p0},
      {0x1.23456789abcdep-1000, 0x1.abcdef0123456p20},
  };
  const double signs[] = {1.0, -1.0};
  struct Mode {
    mpfr::RoundingMode hardware;
    mpfr_rnd_t reference;
  };
  const Mode modes[] = {{mpfr::RoundingMode::Nearest, MPFR_RNDN},
                        {mpfr::RoundingMode::Upward, MPFR_RNDU},
                        {mpfr::RoundingMode::Downward, MPFR_RNDD},
                        {mpfr::RoundingMode::TowardZero, MPFR_RNDZ}};
  mpfr_t y, x, reference;
  // Preserve atan(q)-q even for q near 2^-2098. A low-precision oracle
  // would round it to q and give the wrong directed-rounding expectation.
  mpfr_inits2(4608, y, x, reference, (mpfr_ptr)0);
  for (const auto &input : inputs) {
    for (double sy : signs) {
      for (double sx : signs) {
        double yd = sy * input.x, xd = sx * input.y;
        mpfr_set_d(y, yd, MPFR_RNDN);
        mpfr_set_d(x, xd, MPFR_RNDN);
        mpfr_atan2(reference, y, x, MPFR_RNDN);
        for (const auto &mode : modes) {
          mpfr::ForceRoundingMode rounding(mode.hardware);
          if (!rounding.success)
            continue;
          EXPECT_FP_EQ(mpfr_get_d(reference, mode.reference),
                       LIBC_NAMESPACE::atan2(yd, xd));
        }
      }
    }
  }
  mpfr_clears(y, x, reference, (mpfr_ptr)0);
}

TEST_F(LlvmLibcAtan2Test, TrickyInputs) {
  mpfr::BinaryInput<double> inputs[] = {
      {0x1.0853408534085p-2, 0x1.e7b54166c6126p-2},
      {FPBits::inf().get_val(), 0x0.0000000000001p-1022},
  };

  for (mpfr::BinaryInput<double> &input : inputs) {
    double x = input.x;
    double y = input.y;
    mpfr::RoundingMode rm = mpfr::RoundingMode::Downward;
    mpfr::ForceRoundingMode rr(rm);
    ASSERT_MPFR_MATCH(mpfr::Operation::Atan2, input,
                      LIBC_NAMESPACE::atan2(x, y), 0.5, rm);
    input.x = -input.x;
    ASSERT_MPFR_MATCH_ALL_ROUNDING(mpfr::Operation::Atan2, input,
                                   LIBC_NAMESPACE::atan2(-x, y), 0.5);
    input.y = -input.y;
    ASSERT_MPFR_MATCH_ALL_ROUNDING(mpfr::Operation::Atan2, input,
                                   LIBC_NAMESPACE::atan2(-x, -y), 0.5);
    input.x = -input.x;
    ASSERT_MPFR_MATCH_ALL_ROUNDING(mpfr::Operation::Atan2, input,
                                   LIBC_NAMESPACE::atan2(x, -y), 0.5);
  }
}

TEST_F(LlvmLibcAtan2Test, InDoubleRange) {
  constexpr uint64_t X_COUNT = 123;
  constexpr uint64_t X_START = FPBits(0.25).uintval();
  constexpr uint64_t X_STOP = FPBits(4.0).uintval();
  constexpr uint64_t X_STEP = (X_STOP - X_START) / X_COUNT;

  constexpr uint64_t Y_COUNT = 137;
  constexpr uint64_t Y_START = FPBits(0.25).uintval();
  constexpr uint64_t Y_STOP = FPBits(4.0).uintval();
  constexpr uint64_t Y_STEP = (Y_STOP - Y_START) / Y_COUNT;

  auto test = [&](mpfr::RoundingMode rounding_mode) {
    mpfr::ForceRoundingMode __r(rounding_mode);
    if (!__r.success)
      return;

    uint64_t fails = 0;
    uint64_t finite_count = 0;
    uint64_t total_count = 0;
    double failed_x = 0.0, failed_y = 0.0, failed_r = 0.0;
    double tol = 0.5;

    for (uint64_t i = 0, v = X_START; i <= X_COUNT; ++i, v += X_STEP) {
      double x = FPBits(v).get_val();
      if (FPBits(x).is_inf_or_nan() || x < 0.0)
        continue;

      for (uint64_t j = 0, w = Y_START; j <= Y_COUNT; ++j, w += Y_STEP) {
        double y = FPBits(w).get_val();
        if (FPBits(y).is_inf_or_nan())
          continue;

        double result = LIBC_NAMESPACE::atan2(x, y);
        ++total_count;
        if (FPBits(result).is_inf_or_nan())
          continue;

        ++finite_count;
        mpfr::BinaryInput<double> inputs{x, y};

        if (!TEST_MPFR_MATCH_ROUNDING_SILENTLY(mpfr::Operation::Atan2, inputs,
                                               result, 0.5, rounding_mode)) {
          ++fails;
          while (!TEST_MPFR_MATCH_ROUNDING_SILENTLY(
              mpfr::Operation::Atan2, inputs, result, tol, rounding_mode)) {
            failed_x = x;
            failed_y = y;
            failed_r = result;

            if (tol > 1000.0)
              break;

            tol *= 2.0;
          }
        }
      }
    }
    if (fails || (finite_count < total_count)) {
      tlog << " Atan2 failed: " << fails << "/" << finite_count << "/"
           << total_count << " tests.\n"
           << "   Max ULPs is at most: " << static_cast<uint64_t>(tol) << ".\n";
    }
    if (fails) {
      mpfr::BinaryInput<double> inputs{failed_x, failed_y};
      EXPECT_MPFR_MATCH(mpfr::Operation::Atan2, inputs, failed_r, 0.5,
                        rounding_mode);
    }
  };

  tlog << " Test Rounding To Nearest...\n";
  test(mpfr::RoundingMode::Nearest);

  tlog << " Test Rounding Downward...\n";
  test(mpfr::RoundingMode::Downward);

  tlog << " Test Rounding Upward...\n";
  test(mpfr::RoundingMode::Upward);

  tlog << " Test Rounding Toward Zero...\n";
  test(mpfr::RoundingMode::TowardZero);
}
