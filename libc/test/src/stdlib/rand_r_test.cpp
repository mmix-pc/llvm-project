//===-- Unittests for rand_r -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/stdlib/rand_r.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcRandRTest, VectorsAndIndependentStates) {
  unsigned zero = 0, one = 1;
  const unsigned expected[] = {270369, 67634689, 499951813, 307599695};
  libc_errno = EDOM;
  for (unsigned value : expected) {
    EXPECT_EQ(LIBC_NAMESPACE::rand_r(&zero), static_cast<int>(value));
    EXPECT_EQ(LIBC_NAMESPACE::rand_r(&one), static_cast<int>(value));
    EXPECT_EQ(zero, one);
  }
  unsigned seed = ~0U, copy = seed, unrelated = 37;
  for (unsigned i = 0; i < 200; ++i) {
    int first = LIBC_NAMESPACE::rand_r(&seed);
    LIBC_NAMESPACE::rand_r(&unrelated);
    EXPECT_GE(first, 0);
    EXPECT_EQ(first, LIBC_NAMESPACE::rand_r(&copy));
  }
  EXPECT_EQ(static_cast<int>(libc_errno), EDOM);
}
