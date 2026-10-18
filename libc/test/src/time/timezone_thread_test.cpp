//===-- Concurrent timezone snapshots ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_create.h"
#include "src/pthread/pthread_join.h"
#include "src/time/localtime_r.h"
#include "src/time/mktime.h"
#include "src/time/strftime.h"
#include "src/time/tzset.h"
#include "test/UnitTest/Test.h"
#include "test/src/time/TimezoneGuard.h"

TEST(LlvmLibcTimezoneThreadTest, ConcurrentRefreshAndRoundTrips) {
  LIBC_NAMESPACE::testing::TimezoneGuard timezone_guard;
  ASSERT_TRUE(timezone_guard.set("EST5EDT,M3.2.0,M11.1.0"));
  struct State {
    time_t instant;
    int is_dst;
    bool passed;
  } states[] = {{1710053999, 0, true}, {1710054000, 1, true}};
  auto worker = [](void *arg) -> void * {
    auto &state = *static_cast<State *>(arg);
    for (unsigned i = 0; i < 1024; ++i) {
      tm local{};
      char buffer[64];
      if (LIBC_NAMESPACE::localtime_r(&state.instant, &local) != &local ||
          local.tm_isdst != state.is_dst ||
          LIBC_NAMESPACE::mktime(&local) != state.instant ||
          LIBC_NAMESPACE::strftime(buffer, sizeof(buffer), "%z %Z %s",
                                   &local) == 0) {
        state.passed = false;
        break;
      }
    }
    return nullptr;
  };
  pthread_t threads[2];
  ASSERT_EQ(
      LIBC_NAMESPACE::pthread_create(&threads[0], nullptr, worker, &states[0]),
      0);
  int second =
      LIBC_NAMESPACE::pthread_create(&threads[1], nullptr, worker, &states[1]);
  for (unsigned i = 0; i < 1024; ++i)
    LIBC_NAMESPACE::tzset();
  EXPECT_EQ(LIBC_NAMESPACE::pthread_join(threads[0], nullptr), 0);
  if (second == 0)
    EXPECT_EQ(LIBC_NAMESPACE::pthread_join(threads[1], nullptr), 0);
  ASSERT_EQ(second, 0);
  EXPECT_TRUE(states[0].passed);
  EXPECT_TRUE(states[1].passed);
}
