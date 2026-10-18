//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/locale_macros.h"
#include "src/locale/freelocale.h"
#include "src/locale/newlocale.h"
#include "src/pthread/pthread_create.h"
#include "src/pthread/pthread_join.h"
#include "src/string/strcmp.h"
#include "src/string/strerror.h"
#include "src/string/strerror_l.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcStrerrorLThreadTest, OtherThreadCannotOverwriteMessage) {
  locale_t loc = LIBC_NAMESPACE::newlocale(LC_ALL_MASK, "C", nullptr);
  ASSERT_NE(loc, nullptr);
  const char *message = LIBC_NAMESPACE::strerror_l(-1234, loc);
  struct State {
    locale_t loc;
    bool passed;
  } state{loc, false};
  auto worker = [](void *arg) -> void * {
    auto &state = *static_cast<State *>(arg);
    state.passed =
        LIBC_NAMESPACE::strcmp(LIBC_NAMESPACE::strerror_l(-5678, state.loc),
                               "Unknown error -5678") == 0;
    state.passed &= LIBC_NAMESPACE::strcmp(LIBC_NAMESPACE::strerror(-9012),
                                          "Unknown error -9012") == 0;
    return nullptr;
  };
  pthread_t thread;
  ASSERT_EQ(LIBC_NAMESPACE::pthread_create(&thread, nullptr, worker, &state), 0);
  ASSERT_EQ(LIBC_NAMESPACE::pthread_join(thread, nullptr), 0);
  EXPECT_TRUE(state.passed);
  EXPECT_STREQ(message, "Unknown error -1234");
  LIBC_NAMESPACE::freelocale(loc);
}
