//===-- Unittests for sqrt ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SqrtTest.h"

#include "src/math/sqrt.h"

// Check the public error contract separately from internal square-root kernels.

LIST_SQRT_TESTS(double, LIBC_NAMESPACE::sqrt);

TEST_F(LlvmLibcSqrtTest, Errors) { test_errors(&LIBC_NAMESPACE::sqrt); }
