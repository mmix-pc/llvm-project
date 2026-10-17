//===-- Unittests for fmaf ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FmaTest.h"

#include "src/math/fmaf.h"

LIST_FMA_TESTS(float, LIBC_NAMESPACE::fmaf)

TEST_F(LlvmLibcFmaTest, InexactSubnormalSum) {
  using LIBC_NAMESPACE::fputil::testing::RoundingMode;
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_MODE(
      0x1p-149f, LIBC_NAMESPACE::fmaf(-0x1p-120f, 0x1p-120f, 0x1p-149f),
      FE_UNDERFLOW | FE_INEXACT, RoundingMode::Nearest);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_MODE(
      0x1p-149f, LIBC_NAMESPACE::fmaf(-0x1p-120f, 0x1p-120f, 0x1p-149f),
      FE_UNDERFLOW | FE_INEXACT, RoundingMode::Upward);
  EXPECT_FP_EQ_WITH_EXCEPTION_ROUNDING_MODE(
      -0x1p-149f, LIBC_NAMESPACE::fmaf(0x1p-120f, 0x1p-120f, -0x1p-149f),
      FE_UNDERFLOW | FE_INEXACT, RoundingMode::Downward);
}
