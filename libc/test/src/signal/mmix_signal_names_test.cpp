//===-- Tests for MMIX public signal name conversion
//-----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/signal_macros.h"
#include "src/signal/sig2str.h"
#include "src/signal/str2sig.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcMmixSignalNamesTest, PublicRoundTrip) {
  for (int n = 1; n <= SIGRTMAX; ++n) {
    char name[SIG2STR_MAX] = "unchanged";
    int value = -1;
    if (n >= 32 && n <= 34) {
      EXPECT_EQ(-1, LIBC_NAMESPACE::sig2str(n, name));
      EXPECT_STREQ("unchanged", name);
      continue;
    }
    ASSERT_EQ(0, LIBC_NAMESPACE::sig2str(n, name));
    ASSERT_EQ(0, LIBC_NAMESPACE::str2sig(name, &value));
    EXPECT_EQ(n, value);
  }
}

TEST(LlvmLibcMmixSignalNamesTest, GrammarAndBounds) {
  const char *invalid[] = {
      "",        "0",        "32",       "33",
      "34",      "65",       "SIGTERM",  "term",
      "+15",     "15 ",      "RTMIN+",   "RTMIN-1",
      "RTMAX+1", "RTMIN+30", "RTMAX-30", "9999999999999999999999999"};
  for (const char *name : invalid) {
    int value = 123;
    EXPECT_EQ(-1, LIBC_NAMESPACE::str2sig(name, &value));
    EXPECT_EQ(123, value);
  }
  int value = -1;
  EXPECT_EQ(0, LIBC_NAMESPACE::str2sig("00015", &value));
  EXPECT_EQ(SIGTERM, value);
  EXPECT_EQ(0, LIBC_NAMESPACE::str2sig("IOT", &value));
  EXPECT_EQ(SIGABRT, value);
  EXPECT_EQ(0, LIBC_NAMESPACE::str2sig("IO", &value));
  EXPECT_EQ(SIGPOLL, value);
  char name[SIG2STR_MAX];
  EXPECT_EQ(0, LIBC_NAMESPACE::sig2str(SIGRTMIN + 14, name));
  EXPECT_STREQ("RTMIN+14", name);
  EXPECT_EQ(0, LIBC_NAMESPACE::sig2str(SIGRTMIN + 15, name));
  EXPECT_STREQ("RTMAX-14", name);
}
