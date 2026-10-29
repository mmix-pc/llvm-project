//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "wait_status_macros_test.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcWaitStatusMacrosTest, StatusAndSingleEvaluation) {
  EXPECT_EQ(check_wait_status(), 0);
}
