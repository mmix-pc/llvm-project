//===-- Tests for pipe stream setup allocation failures --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/fcntl.h"
#include "src/stdio/fgetc.h"
#include "src/stdio/pclose.h"
#include "src/stdio/popen.h"
#include "src/sys/wait/waitpid.h"
#include "src/unistd/close.h"
#include "test/IntegrationTest/test.h"
#include <fcntl.h>
#include <stddef.h>
#include <sys/wait.h>

namespace {
bool tracking;
unsigned calls, fail_at, live;
void *owned[8];
}

extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (tracking && ++calls == fail_at)
    return nullptr;
  void *pointer = __real_malloc(size);
  if (tracking && pointer) {
    for (void *&slot : owned) {
      if (!slot) {
        slot = pointer;
        ++live;
        break;
      }
    }
  }
  return pointer;
}
extern "C" void __wrap_free(void *pointer) {
  if (pointer) {
    for (void *&slot : owned) {
      if (slot == pointer) {
        slot = nullptr;
        --live;
        break;
      }
    }
  }
  __real_free(pointer);
}

TEST_MAIN() {
  // The production path allocates PipeFile and three spawn actions before
  // creating any child. Each failure must consume all resources acquired so far.
  for (unsigned failure = 1; failure <= 4; ++failure) {
    int next = LIBC_NAMESPACE::fcntl(2, F_DUPFD, 3);
    ASSERT_TRUE(next >= 3);
    ASSERT_EQ(LIBC_NAMESPACE::close(next), 0);
    calls = live = 0;
    fail_at = failure;
    tracking = true;
    ::FILE *stream = LIBC_NAMESPACE::popen("exit 99", "r");
    tracking = false;
    ASSERT_EQ(stream, nullptr);
    ASSERT_EQ(libc_errno, ENOMEM);
    ASSERT_EQ(calls, failure);
    ASSERT_EQ(live, 0U);
    int fd = LIBC_NAMESPACE::fcntl(2, F_DUPFD, 3);
    ASSERT_EQ(fd, next);
    ASSERT_EQ(LIBC_NAMESPACE::close(fd), 0);
    int status;
    ASSERT_EQ(LIBC_NAMESPACE::waitpid(-1, &status, WNOHANG), -1);
    ASSERT_EQ(libc_errno, ECHILD);
  }
  // Recovery exercises the unchanged registry after every failed transaction.
  ::FILE *stream = LIBC_NAMESPACE::popen("printf x; exit 7", "r");
  ASSERT_NE(stream, nullptr);
  ASSERT_EQ(LIBC_NAMESPACE::fgetc(stream), 'x');
  int status = LIBC_NAMESPACE::pclose(stream);
  ASSERT_TRUE(status >= 0 && WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 7);
  return 0;
}
