//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/wchar_macros.h"
#include "src/wctype/towctrans.h"
#include "src/wctype/towlower.h"
#include "src/wctype/towupper.h"
#include "src/wctype/wctrans.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcWctransTest, NamesAndMappings) {
  auto lower = LIBC_NAMESPACE::wctrans("tolower");
  auto upper = LIBC_NAMESPACE::wctrans("toupper");
  EXPECT_TRUE(lower != 0);
  EXPECT_TRUE(upper != 0);
  EXPECT_TRUE(lower != upper);
  EXPECT_TRUE(LIBC_NAMESPACE::wctrans("") == 0);
  EXPECT_TRUE(LIBC_NAMESPACE::wctrans("TOLOWER") == 0);
  for (wint_t c = 0; c < 256; ++c) {
    EXPECT_EQ(LIBC_NAMESPACE::towctrans(c, lower), LIBC_NAMESPACE::towlower(c));
    EXPECT_EQ(LIBC_NAMESPACE::towctrans(c, upper), LIBC_NAMESPACE::towupper(c));
  }
  EXPECT_EQ(LIBC_NAMESPACE::towctrans(WEOF, lower), wint_t(WEOF));
  EXPECT_EQ(LIBC_NAMESPACE::towctrans(WEOF, upper), wint_t(WEOF));
}
