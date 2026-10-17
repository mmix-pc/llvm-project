//===-- Unittests for sqrtf------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SqrtTest.h"

#include "src/math/sqrtf.h"

// Check the public error contract separately from internal square-root kernels.

LIST_SQRT_TESTS(float, LIBC_NAMESPACE::sqrtf);

TEST_F(LlvmLibcSqrtTest, Errors) { test_errors(&LIBC_NAMESPACE::sqrtf); }
