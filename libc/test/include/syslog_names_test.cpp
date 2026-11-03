// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "syslog_names_test.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcSyslogNamesTest, TypesAndStorage) {
  EXPECT_EQ(check_syslog_names(), 0);
}
