//===-- Tests for signgam publication -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/FPUtil/FPBits.h"
#include "src/math/lgamma.h"
#include "src/math/lgamma_r.h"
#include "src/math/lgammaf.h"
#include "src/math/signgam.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcSigngamTest, KnownSignsAcrossPrecisions) {
  struct Case {
    double input;
    int sign;
  };
  const Case cases[] = {{1.0, 1},   {0.5, 1},  {-0.5, -1},  {-1.5, 1},
                        {-2.5, -1}, {-3.5, 1}, {-199.5, 1}, {-200.5, -1},
                        {0.0, 1},   {-0.0, -1}};
  for (const auto &c : cases) {
    LIBC_NAMESPACE::signgam = 42;
    LIBC_NAMESPACE::lgamma(c.input);
    EXPECT_EQ(LIBC_NAMESPACE::signgam, c.sign);
    LIBC_NAMESPACE::signgam = 42;
    LIBC_NAMESPACE::lgammaf(static_cast<float>(c.input));
    EXPECT_EQ(LIBC_NAMESPACE::signgam, c.sign);
  }
}

TEST(LlvmLibcSigngamTest, ReentrantResultDoesNotChangeGlobal) {
  LIBC_NAMESPACE::signgam = 42;
  int sign = 0;
  double value = LIBC_NAMESPACE::lgamma_r(-0.5, &sign);
  EXPECT_EQ(sign, -1);
  EXPECT_EQ(LIBC_NAMESPACE::signgam, 42);
  double public_value = LIBC_NAMESPACE::lgamma(-0.5);
  EXPECT_EQ(LIBC_NAMESPACE::fputil::FPBits<double>(value).uintval(),
            LIBC_NAMESPACE::fputil::FPBits<double>(public_value).uintval());
  EXPECT_EQ(LIBC_NAMESPACE::signgam, -1);
}
