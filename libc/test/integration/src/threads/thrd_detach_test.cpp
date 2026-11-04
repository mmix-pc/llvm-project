//===-- Tests for thrd_detach ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/thread.h"
#include "src/threads/thrd_create.h"
#include "src/threads/thrd_detach.h"
#include "test/IntegrationTest/test.h"
#include <threads.h>

static int run(void *) { return 0; }

TEST_MAIN() {
  thrd_t thread;
  ASSERT_EQ(LIBC_NAMESPACE::thrd_create(&thread, run, nullptr),
            int(thrd_success));
  // A terminated joinable thread remains valid until detached or joined.
  reinterpret_cast<LIBC_NAMESPACE::Thread *>(&thread)->wait();
  ASSERT_EQ(LIBC_NAMESPACE::thrd_detach(thread), int(thrd_success));
  return 0;
}
