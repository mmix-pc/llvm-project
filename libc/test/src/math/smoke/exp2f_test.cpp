//===-- Unittests for exp2f -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/math_macros.h"
#include "hdr/stdint_proxy.h"
#include "src/__support/FPUtil/FPBits.h"
#include "src/math/exp2f.h"
#include "test/UnitTest/FPMatcher.h"
#include "test/UnitTest/Test.h"

using LlvmLibcExp2fTest = LIBC_NAMESPACE::testing::FPTest<float>;

TEST_F(LlvmLibcExp2fTest, SpecialNumbers) {
  EXPECT_FP_EQ_WITH_EXCEPTION(aNaN, LIBC_NAMESPACE::exp2f(sNaN), FE_INVALID);
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ_ALL_ROUNDING(aNaN, LIBC_NAMESPACE::exp2f(aNaN));
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ_ALL_ROUNDING(inf, LIBC_NAMESPACE::exp2f(inf));
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ_ALL_ROUNDING(0.0f, LIBC_NAMESPACE::exp2f(neg_inf));
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ_ALL_ROUNDING(1.0f, LIBC_NAMESPACE::exp2f(0.0f));
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ_ALL_ROUNDING(1.0f, LIBC_NAMESPACE::exp2f(-0.0f));
  EXPECT_MATH_ERRNO(0);

  EXPECT_FP_EQ_ALL_ROUNDING(2.0f, LIBC_NAMESPACE::exp2f(1.0f));
  EXPECT_FP_EQ_ALL_ROUNDING(0.5f, LIBC_NAMESPACE::exp2f(-1.0f));
  EXPECT_FP_EQ_ALL_ROUNDING(4.0f, LIBC_NAMESPACE::exp2f(2.0f));
  EXPECT_FP_EQ_ALL_ROUNDING(0.25f, LIBC_NAMESPACE::exp2f(-2.0f));
}

TEST_F(LlvmLibcExp2fTest, Overflow) {
  EXPECT_FP_EQ_WITH_EXCEPTION(
      inf, LIBC_NAMESPACE::exp2f(FPBits(0x7f7fffffU).get_val()), FE_OVERFLOW);
  EXPECT_MATH_ERRNO(ERANGE);

  EXPECT_FP_EQ_WITH_EXCEPTION(
      inf, LIBC_NAMESPACE::exp2f(FPBits(0x43000000U).get_val()), FE_OVERFLOW);
  EXPECT_MATH_ERRNO(ERANGE);

  EXPECT_FP_EQ_WITH_EXCEPTION(
      inf, LIBC_NAMESPACE::exp2f(FPBits(0x43000001U).get_val()), FE_OVERFLOW);
  EXPECT_MATH_ERRNO(ERANGE);
}

TEST_F(LlvmLibcExp2fTest, DirectedOverflowReporting) {
  using namespace LIBC_NAMESPACE::fputil::testing;
  for (auto mode : ROUNDING_MODES) {
    ForceRoundingMode rounding(mode);
    if (!rounding.success)
      continue;
    const float inputs[] = {128.0f, 10000.0f, max_normal};
    for (float x : inputs) {
      LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
      LIBC_NAMESPACE::fputil::raise_except(FE_INVALID);
      libc_errno = 0;
      const bool finite =
          mode == RoundingMode::Downward || mode == RoundingMode::TowardZero;
      EXPECT_FP_EQ(finite ? max_normal : inf, LIBC_NAMESPACE::exp2f(x));
      EXPECT_MATH_ERRNO(ERANGE);
      if (math_errhandling & MATH_ERREXCEPT)
        EXPECT_EQ(FE_INVALID | FE_OVERFLOW, LIBC_NAMESPACE::fputil::test_except(
                                                FE_INVALID | FE_OVERFLOW));
    }
    LIBC_NAMESPACE::fputil::clear_except(FE_ALL_EXCEPT);
    libc_errno = EDOM;
    EXPECT_FP_EQ(inf, LIBC_NAMESPACE::exp2f(inf));
    EXPECT_MATH_ERRNO(EDOM);
    EXPECT_EQ(0, LIBC_NAMESPACE::fputil::test_except(FE_ALL_EXCEPT));
  }
}

#ifdef LIBC_TEST_FTZ_DAZ

using namespace LIBC_NAMESPACE::testing;

TEST_F(LlvmLibcExp2fTest, FTZMode) {
  ModifyMXCSR mxcsr(FTZ);

  EXPECT_FP_EQ(1.0f, LIBC_NAMESPACE::exp2f(min_denormal));
  EXPECT_FP_EQ(1.0f, LIBC_NAMESPACE::exp2f(max_denormal));
}

TEST_F(LlvmLibcExp2fTest, DAZMode) {
  ModifyMXCSR mxcsr(DAZ);

  EXPECT_FP_EQ(1.0f, LIBC_NAMESPACE::exp2f(min_denormal));
  EXPECT_FP_EQ(1.0f, LIBC_NAMESPACE::exp2f(max_denormal));
}

TEST_F(LlvmLibcExp2fTest, FTZDAZMode) {
  ModifyMXCSR mxcsr(FTZ | DAZ);

  EXPECT_FP_EQ(1.0f, LIBC_NAMESPACE::exp2f(min_denormal));
  EXPECT_FP_EQ(1.0f, LIBC_NAMESPACE::exp2f(max_denormal));
}

#endif
