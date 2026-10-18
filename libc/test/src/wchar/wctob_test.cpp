//===-- Unittests for wctob ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/stdio_macros.h" //for EOF
#include "src/wchar/wctob.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcWctob, DefaultLocale) {
  // Full builds own the byte-preserving C locale; overlay uses UTF-8.
#ifdef LIBC_FULL_BUILD
  constexpr wint_t limit = 256;
#else
  constexpr wint_t limit = 128;
#endif
  for (wint_t c = 0; c < 32767; ++c) {
    if (c < limit)
      EXPECT_EQ(LIBC_NAMESPACE::wctob(c), static_cast<int>(c));
    else
      EXPECT_EQ(LIBC_NAMESPACE::wctob(c), EOF);
  }
}
