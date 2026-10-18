//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/new.h"
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#include "src/__support/threads/linux/mmix/loader_cancellation.h"
#include "src/stdio/fdopen.h"
#include "src/stdio/fwrite.h"
#include "src/stdio/setvbuf.h"
#include "src/stdlib/_Exit.h"
#include "src/stdlib/at_quick_exit.h"
#include "src/stdlib/atexit.h"
#include "src/stdlib/free.h"
#include "src/stdlib/malloc.h"
#include "src/stdlib/quick_exit.h"
#include "src/unistd/close.h"
#include "src/unistd/pipe.h"
#include "src/unistd/read.h"
#include "test/UnitTest/Test.h"
#include <stdlib.h>

namespace {
int depth, pins, callbacks;
bool fail_pin, fail_allocation, terminating;
int descriptors[2];
void check(bool ok) {
  if (!ok)
    __builtin_trap();
}
long enter(uint64_t *saved) {
  *saved = depth;
  ++depth;
  return 0;
}
long leave(uint64_t saved) {
  check(depth == int(saved) + 1);
  --depth;
  return 0;
}
} // namespace
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
loader::LoaderCancellation loader_cancellation_hooks() {
  return {enter, leave, nullptr, nullptr, nullptr, nullptr};
}
long retain_shared_tls_destructor(void *dso, uintptr_t function,
                                  uint64_t *pin) {
  check(!dso && function && depth > 0);
  if (fail_pin)
    return -1;
  *pin = ++pins;
  return 0;
}
long release_shared_tls_destructor(uint64_t pin) {
  check(depth > 0 && pin != 0 && pins > 0);
  --pins;
  return 0;
}
} // namespace mmix
namespace internal {
void begin_process_exit(int status) {
  if (terminating)
    LIBC_NAMESPACE::_Exit(status);
  terminating = true;
}
} // namespace internal
void *quick_test_malloc(size_t size) {
  check(depth > 0);
  return fail_allocation ? nullptr : ::malloc(size);
}
void quick_test_free(void *ptr) { ::free(ptr); }
} // namespace LIBC_NAMESPACE_DECL
#define LIBC_MMIX_SHARED_STARTUP
#define LIBC_MMIX_QUICK_EXIT_CANCELLATION
#define malloc quick_test_malloc
#define free quick_test_free
#define at_quick_exit mmix_at_quick_exit
#define quick_exit mmix_quick_exit
#include "src/stdlib/linux/mmix/quick_exit.cpp"
#undef quick_exit
#undef at_quick_exit
#undef malloc
#undef free

static void poison() { __builtin_trap(); }

TEST(LlvmLibcMMIXQuickExitTest, RegistrationFailures) {
  EXPECT_EQ(LIBC_NAMESPACE::mmix_at_quick_exit(nullptr), -1);
  fail_pin = true;
  EXPECT_EQ(LIBC_NAMESPACE::mmix_at_quick_exit(poison), -1);
  fail_pin = false;
  fail_allocation = true;
  EXPECT_EQ(LIBC_NAMESPACE::mmix_at_quick_exit(poison), -1);
  fail_allocation = false;
  EXPECT_EQ(pins, 0);
  EXPECT_EQ(depth, 0);
}

TEST(LlvmLibcMMIXQuickExitTest, ReverseOrderAndRegistrationDuringDrain) {
  EXPECT_EXITS(
      [] {
        check(LIBC_NAMESPACE::atexit(poison) == 0);
        check(LIBC_NAMESPACE::mmix_at_quick_exit(+[] {
                check(callbacks == 2 && pins == 1 && depth > 0);
              }) == 0);
        check(LIBC_NAMESPACE::mmix_at_quick_exit(+[] {
                check(callbacks++ == 0 && pins == 2);
                check(LIBC_NAMESPACE::mmix_at_quick_exit(+[] {
                        check(callbacks++ == 1 && pins == 2 && depth > 0);
                      }) == 0);
              }) == 0);
        LIBC_NAMESPACE::mmix_quick_exit(23);
      },
      23);
}

TEST(LlvmLibcMMIXQuickExitTest, ManyCallbacks) {
  EXPECT_EXITS(
      [] {
        check(LIBC_NAMESPACE::mmix_at_quick_exit(
                  +[] { check(callbacks == 256 && pins == 1); }) == 0);
        for (int i = 0; i < 256; ++i)
          check(LIBC_NAMESPACE::mmix_at_quick_exit(+[] { ++callbacks; }) == 0);
        LIBC_NAMESPACE::mmix_quick_exit(0);
      },
      0);
}

TEST(LlvmLibcMMIXQuickExitTest, RecursiveTerminationSkipsRemainingCallbacks) {
  EXPECT_EXITS(
      [] {
        check(LIBC_NAMESPACE::mmix_at_quick_exit(poison) == 0);
        check(LIBC_NAMESPACE::mmix_at_quick_exit(
                  +[] { LIBC_NAMESPACE::mmix_quick_exit(42); }) == 0);
        LIBC_NAMESPACE::mmix_quick_exit(0);
      },
      42);
}

TEST(LlvmLibcMMIXQuickExitTest, DoesNotFlushStreams) {
  ASSERT_EQ(LIBC_NAMESPACE::pipe(descriptors), 0);
  EXPECT_EXITS(
      [] {
        auto *stream = LIBC_NAMESPACE::fdopen(descriptors[1], "w");
        check(stream != nullptr);
        char buffer[64];
        check(LIBC_NAMESPACE::setvbuf(stream, buffer, _IOFBF, sizeof(buffer)) ==
              0);
        check(LIBC_NAMESPACE::fwrite("unflushed", 1, 9, stream) == 9);
        LIBC_NAMESPACE::mmix_quick_exit(0);
      },
      0);
  ASSERT_EQ(LIBC_NAMESPACE::close(descriptors[1]), 0);
  char byte;
  EXPECT_EQ(LIBC_NAMESPACE::read(descriptors[0], &byte, 1), ssize_t(0));
  ASSERT_EQ(LIBC_NAMESPACE::close(descriptors[0]), 0);
}
