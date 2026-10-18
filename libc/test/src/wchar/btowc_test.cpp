//===-- Unittests for btowc ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/wchar_macros.h" // for WEOF
#include "src/wchar/btowc.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcBtowc, DefaultLocale) {
  // Full builds own the byte-preserving C locale; overlay uses UTF-8.
#ifdef LIBC_FULL_BUILD
  constexpr int limit = 256;
#else
  constexpr int limit = 128;
#endif
  for (int c = -1; c <= 256; ++c) {
    if (c >= 0 && c < limit)
      EXPECT_EQ(LIBC_NAMESPACE::btowc(c), static_cast<wint_t>(c));
    else
      EXPECT_EQ(LIBC_NAMESPACE::btowc(c), WEOF);
  }
}
