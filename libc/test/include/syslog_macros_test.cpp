//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#define SYSLOG_NAMES
#define SYSLOG_NAMES_CONST
#include "include/llvm-libc-macros/syslog-macros.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcSyslogMacrosTest, EncodedPrioritiesAndSingleEvaluation) {
  for (int facility = 0; facility < LOG_NFACILITIES; ++facility) {
    for (int priority = LOG_EMERG; priority <= LOG_DEBUG; ++priority) {
      int f = facility << 3, p = priority;
      int encoded = LOG_MAKEPRI(f++, p++);
      EXPECT_EQ(f, (facility << 3) + 1);
      EXPECT_EQ(p, priority + 1);
      EXPECT_EQ(LOG_PRI(encoded++), priority);
      EXPECT_EQ(encoded, ((facility << 3) | priority) + 1);
      encoded = (facility << 3) | priority;
      EXPECT_EQ(LOG_FAC(encoded++), facility);
      EXPECT_EQ(encoded, ((facility << 3) | priority) + 1);
    }
  }
}

TEST(LlvmLibcSyslogMacrosTest, NamesAndSentinels) {
  unsigned priorities = 0, facilities = 0;
  for (const CODE *entry = prioritynames; entry->c_name; ++entry) {
    if (entry->c_val != INTERNAL_NOPRI)
      priorities |= 1U << entry->c_val;
  }
  for (const CODE *entry = facilitynames; entry->c_name; ++entry) {
    if (entry->c_val != INTERNAL_MARK)
      facilities |= 1U << LOG_FAC(entry->c_val);
  }
  EXPECT_EQ(priorities, 0xffU);
  EXPECT_EQ(facilities, 0xff0fffU);
  EXPECT_EQ(prioritynames[12].c_val, -1);
  EXPECT_EQ(facilitynames[22].c_val, -1);
  EXPECT_STREQ(prioritynames[5].c_name, "error");
  EXPECT_EQ(prioritynames[5].c_val, LOG_ERR);
  EXPECT_STREQ(facilitynames[11].c_name, "syslog");
  EXPECT_EQ(facilitynames[11].c_val, LOG_SYSLOG);
  EXPECT_EQ(LOG_AUTHPRIV, 10 << 3);
  EXPECT_EQ(LOG_FTP, 11 << 3);
}
