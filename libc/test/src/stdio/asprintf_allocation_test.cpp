//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "hdr/func/realloc.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

static unsigned allocations, fail_at;
static void *test_malloc(size_t size) {
  return ++allocations == fail_at ? nullptr : ::malloc(size);
}
static void *test_realloc(void *ptr, size_t size) {
  return ++allocations == fail_at ? nullptr : ::realloc(ptr, size);
}

// Inject allocation failure into the real formatter, not a replacement engine.
#define malloc test_malloc
#define realloc test_realloc
#include "src/stdio/asprintf.cpp"
#include "src/stdio/vasprintf.cpp"
#undef realloc
#undef malloc

static int format(char **out, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int result = LIBC_NAMESPACE::vasprintf(out, fmt, args);
  va_end(args);
  return result;
}

TEST(LlvmLibcASPrintfAllocationTest, SmallAllocationFailure) {
  allocations = 0;
  fail_at = 1;
  char *out = reinterpret_cast<char *>(1);
  EXPECT_EQ(LIBC_NAMESPACE::asprintf(&out, "%s", "short"), -1);
  EXPECT_EQ(out, static_cast<char *>(nullptr));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
  allocations = 0;
  EXPECT_EQ(format(&out, "%d", 42), -1);
  EXPECT_EQ(out, static_cast<char *>(nullptr));
}

TEST(LlvmLibcASPrintfAllocationTest, GrowthFailureAndRecovery) {
  char *out = nullptr;
  for (unsigned fail = 1; fail <= 3; ++fail) {
    allocations = 0;
    fail_at = fail;
    EXPECT_EQ(format(&out, "%1000s", "x"), -1);
    EXPECT_EQ(out, static_cast<char *>(nullptr));
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
  }
  allocations = 0;
  fail_at = 0;
  EXPECT_EQ(format(&out, "%1000s", "x"), 1000);
  ASSERT_TRUE(out != nullptr);
  EXPECT_EQ(out[999], 'x');
  EXPECT_EQ(out[1000], '\0');
  ::free(out);
}
