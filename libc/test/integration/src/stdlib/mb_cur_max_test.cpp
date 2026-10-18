//===-- Public MB_CUR_MAX provider test -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdlib.h>

#include "test/IntegrationTest/test.h"

// This test deliberately needs no public locale-management entrypoints.
TEST_MAIN([[maybe_unused]] int argc, [[maybe_unused]] char **argv,
          [[maybe_unused]] char **envp) {
  ASSERT_EQ(MB_CUR_MAX, size_t(1));
  return 0;
}
