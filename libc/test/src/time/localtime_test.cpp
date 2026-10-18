//===-- Unittests for localtime -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/signal_macros.h"
#include "src/time/localtime.h"
#include "test/UnitTest/Test.h"
#include "test/src/time/TimezoneGuard.h"

class LlvmLibcLocaltime : public LIBC_NAMESPACE::testing::Test {
  LIBC_NAMESPACE::testing::TimezoneGuard timezone_guard{false};

public:
  void SetUp() override {
    timezone_guard.save();
  }
  void TearDown() override {
    timezone_guard.restore();
  }
};

TEST_F(LlvmLibcLocaltime, ValidUnixTimestamp0) {
  const time_t timer = 0;
  struct tm *result = LIBC_NAMESPACE::localtime(&timer);

  ASSERT_EQ(70, result->tm_year);
  ASSERT_EQ(0, result->tm_mon);
  ASSERT_EQ(1, result->tm_mday);
  ASSERT_EQ(0, result->tm_hour);
  ASSERT_EQ(0, result->tm_min);
  ASSERT_EQ(0, result->tm_sec);
  ASSERT_EQ(4, result->tm_wday);
  ASSERT_EQ(0, result->tm_yday);
  ASSERT_EQ(0, result->tm_isdst);
}

TEST_F(LlvmLibcLocaltime, NullPtr) {
  EXPECT_DEATH([] { LIBC_NAMESPACE::localtime(nullptr); }, WITH_SIGNAL(-1));
}

TEST_F(LlvmLibcLocaltime, ValidUnixTimestamp) {
  const time_t timer = 1756595338;
  struct tm *result = LIBC_NAMESPACE::localtime(&timer);

  ASSERT_EQ(125, result->tm_year);
  ASSERT_EQ(7, result->tm_mon);
  ASSERT_EQ(30, result->tm_mday);
  ASSERT_EQ(23, result->tm_hour);
  ASSERT_EQ(8, result->tm_min);
  ASSERT_EQ(58, result->tm_sec);
  ASSERT_EQ(6, result->tm_wday);
  ASSERT_EQ(241, result->tm_yday);
  ASSERT_EQ(0, result->tm_isdst);
}

TEST_F(LlvmLibcLocaltime, ValidUnixTimestampNegative) {
  const time_t timer = -1756595338;
  struct tm *result = LIBC_NAMESPACE::localtime(&timer);

  ASSERT_EQ(14, result->tm_year);
  ASSERT_EQ(4, result->tm_mon);
  ASSERT_EQ(4, result->tm_mday);
  ASSERT_EQ(0, result->tm_hour);
  ASSERT_EQ(51, result->tm_min);
  ASSERT_EQ(2, result->tm_sec);
  ASSERT_EQ(1, result->tm_wday);
  ASSERT_EQ(123, result->tm_yday);
  ASSERT_EQ(0, result->tm_isdst);
}
