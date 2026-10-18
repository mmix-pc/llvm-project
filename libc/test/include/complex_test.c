//===-- Tests for complex macros in strict C17 ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "include/llvm-libc-macros/complex-macros.h"
#include "test/UnitTest/LibcCTest.h"

_Static_assert(_Generic(_Complex_I, float complex: 1, default: 0),
               "_Complex_I must have float complex type");
_Static_assert(_Generic(I, float complex: 1, default: 0),
               "I must have float complex type");

// Both macros must also be usable in static initializers.
static const float complex unit = _Complex_I;
static const float complex alias = I;

TEST(complex_macros) {
  EXPECT_TRUE(__real__ unit == 0.0f);
  EXPECT_TRUE(__imag__ unit == 1.0f);
  EXPECT_TRUE(alias == unit);
  EXPECT_TRUE(I * I == -1.0f);
}
