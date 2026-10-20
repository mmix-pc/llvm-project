//===-- C++ tests for sys/mman.h function types ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mman_types_test.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcMmanTypesTest, ReadOnlyAddressTypes) {
  EXPECT_EQ(sizeof(mlock(static_cast<const void *>(nullptr), 0)), sizeof(int));
  EXPECT_EQ(sizeof(mlock2(static_cast<const void *>(nullptr), 0, 0)), sizeof(int));
  EXPECT_EQ(sizeof(munlock(static_cast<const void *>(nullptr), 0)), sizeof(int));
}
