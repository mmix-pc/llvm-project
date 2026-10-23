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
#include "test/src/locale/LocaleGuard.h"

static unsigned allocations, fail_at;
static unsigned live_allocations;
static void *test_malloc(size_t size) {
  void *result = ++allocations == fail_at ? nullptr : ::malloc(size);
  if (result)
    ++live_allocations;
  return result;
}
static void *test_realloc(void *ptr, size_t size) {
  return ++allocations == fail_at ? nullptr : ::realloc(ptr, size);
}
static void test_free(void *ptr) {
  if (ptr)
    --live_allocations;
  ::free(ptr);
}

// Inject allocation failure into the real formatter, not a replacement engine.
#define malloc test_malloc
#define realloc test_realloc
#define free test_free
#include "src/stdio/asprintf.cpp"
#include "src/stdio/vasprintf.cpp"
#undef free
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
  EXPECT_EQ(live_allocations, 0U);
}

TEST(LlvmLibcASPrintfAllocationTest, GrowthFailureAndRecovery) {
  char *out = nullptr;
  for (unsigned fail = 1; fail <= 3; ++fail) {
    allocations = 0;
    fail_at = fail;
    EXPECT_EQ(format(&out, "%1000s", "x"), -1);
    EXPECT_EQ(out, static_cast<char *>(nullptr));
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
    EXPECT_EQ(live_allocations, 0U);
  }
  allocations = 0;
  fail_at = 0;
  EXPECT_EQ(format(&out, "%1000s", "x"), 1000);
  ASSERT_TRUE(out != nullptr);
  EXPECT_EQ(out[999], 'x');
  EXPECT_EQ(out[1000], '\0');
  test_free(out);
  EXPECT_EQ(live_allocations, 0U);
}

#ifndef LIBC_COPT_PRINTF_DISABLE_WIDE
TEST(LlvmLibcASPrintfAllocationTest, EncodingFailureReleasesGrowth) {
  LocaleGuard locale;
  allocations = fail_at = 0;
  const wchar_t invalid[] = {L'a', wchar_t(0x110000), L'\0'};
  char *out = nullptr;
  EXPECT_EQ(LIBC_NAMESPACE::asprintf(&out, "%300s%ls", "", invalid), -1);
  EXPECT_GT(allocations, 0U);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EILSEQ);
  EXPECT_EQ(out, static_cast<char *>(nullptr));
  EXPECT_EQ(live_allocations, 0U);
  EXPECT_EQ(format(&out, "%300s%ls", "", invalid), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EILSEQ);
  EXPECT_EQ(out, static_cast<char *>(nullptr));
  EXPECT_EQ(live_allocations, 0U);
}
#endif

TEST(LlvmLibcASPrintfAllocationTest, CountOverflowReleasesGrowth) {
  using namespace LIBC_NAMESPACE::printf_core;
  allocations = fail_at = 0;
  char initial[1];
  ResizingBuffer buffer(initial, sizeof(initial), resize_overflow_hook);
  buffer.buff = static_cast<char *>(test_malloc(32));
  ASSERT_NE(buffer.buff, nullptr);
  char *out = nullptr;
  // Inject only the count at the production ownership-transfer boundary.
  auto result = finish_asprintf(
      &out, buffer,
      size_t(LIBC_NAMESPACE::cpp::numeric_limits<int>::max()) + 1);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(internal_error_to_errno(result.error()), EOVERFLOW);
  EXPECT_EQ(out, static_cast<char *>(nullptr));
  EXPECT_EQ(live_allocations, 0U);
}

TEST(LlvmLibcASPrintfAllocationTest, ResizeRejectsUnrepresentableCount) {
  using namespace LIBC_NAMESPACE::printf_core;
  allocations = fail_at = 0;
  char initial[1];
  ResizingBuffer buffer(initial, sizeof(initial), resize_overflow_hook);
  // No input byte is read and no huge allocation is attempted.
  buffer.buff_cur = LIBC_NAMESPACE::cpp::numeric_limits<int>::max();
  EXPECT_EQ(resize_overflow_hook({"x", 1}, &buffer), OVERFLOW_ERROR);
  EXPECT_EQ(allocations, 0U);
  EXPECT_EQ(buffer.buff, static_cast<char *>(initial));
}
