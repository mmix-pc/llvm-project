//===-- Scanf allocation failure and ownership tests ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "hdr/func/realloc.h"
#include "hdr/stdio_macros.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

static size_t calls, fail_at, live;
static void *scan_malloc(size_t n) {
  if (++calls == fail_at)
    return nullptr;
  void *p = ::malloc(n);
  if (p)
    ++live;
  return p;
}
static void *scan_realloc(void *p, size_t n) {
  if (++calls == fail_at)
    return nullptr;
  bool fresh = p == nullptr;
  void *q = ::realloc(p, n);
  if (q && fresh)
    ++live;
  return q;
}
static void scan_free(void *p) {
  if (p)
    --live;
  ::free(p);
}

// Inject failures into the production storage and token engines only.
#define malloc scan_malloc
#define realloc scan_realloc
#define free scan_free
#include "src/stdio/scanf_core/scanf_main.h"
#include "src/stdio/scanf_core/string_reader.h"
#include "src/stdio/scanf_core/wide_scanf_main.h"
#undef free
#undef realloc
#undef malloc

static int scan(const char *input, const char *format, ...) {
  va_list args;
  va_start(args, format);
  LIBC_NAMESPACE::internal::ArgList list(args);
  va_end(args);
  LIBC_NAMESPACE::scanf_core::StringReader reader(input, size_t(-1));
  return LIBC_NAMESPACE::scanf_core::scanf_main(&reader, format, list);
}

static int scan(const wchar_t *input, const wchar_t *format, ...) {
  struct Source {
    const wchar_t *next;
    wint_t peek() { return *next ? wint_t(*next) : WEOF; }
    void take() { ++next; }
  } source{input};
  va_list args;
  va_start(args, format);
  LIBC_NAMESPACE::internal::ArgList list(args);
  va_end(args);
  return LIBC_NAMESPACE::scanf_core::wide_scanf_main(source, format, list);
}

static void reset(size_t failure) {
  calls = live = 0;
  fail_at = failure;
  LIBC_NAMESPACE::libc_errno = 0;
}

TEST(LlvmLibcScanfAllocationTest, UnpublishedPartialConversion) {
  for (size_t failure = 1; failure <= 3; ++failure) {
    reset(failure);
    char input[100];
    for (size_t i = 0; i < sizeof(input) - 1; ++i)
      input[i] = 'a';
    input[99] = 0;
    char sentinel;
    char *out = &sentinel;
    EXPECT_EQ(scan(input, "%ms", &out), EOF);
    EXPECT_EQ(out, &sentinel);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
    EXPECT_EQ(live, size_t(0));
  }
  reset(0);
  char *out = nullptr;
  EXPECT_EQ(scan("abc", "%5mc", &out), EOF);
  EXPECT_EQ(out, static_cast<char *>(nullptr));
  EXPECT_EQ(live, size_t(0));
}

TEST(LlvmLibcScanfAllocationTest, CompletedAssignmentsRemainOwnedByCaller) {
  reset(2);
  char *first = nullptr, *second = nullptr;
  EXPECT_EQ(scan("first second", "%ms %ms", &first, &second), 1);
  ASSERT_NE(first, nullptr);
  EXPECT_STREQ(first, "first");
  EXPECT_EQ(second, static_cast<char *>(nullptr));
  EXPECT_EQ(live, size_t(1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
  scan_free(first);
  EXPECT_EQ(live, size_t(0));

  reset(1);
  int number = 0;
  EXPECT_EQ(scan("7 text", "%d %ms", &number, &second), 1);
  EXPECT_EQ(number, 7);
  EXPECT_EQ(live, size_t(0));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
}

TEST(LlvmLibcScanfAllocationTest, SuppressedAndMatchingFailuresDoNotAllocate) {
  reset(1);
  EXPECT_EQ(scan("abc", "%*ms"), 0);
  EXPECT_EQ(calls, size_t(0));
  char *out = nullptr;
  EXPECT_EQ(scan("!", "%m[a]", &out), 0);
  EXPECT_EQ(out, static_cast<char *>(nullptr));
  EXPECT_EQ(calls, size_t(0));
  int count = -1;
  EXPECT_EQ(scan("abc", "%n%ms", &count, &out), 0);
  EXPECT_EQ(count, 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
  EXPECT_EQ(live, size_t(0));
}

TEST(LlvmLibcScanfAllocationTest, WideAllocationsAndConversionFailures) {
  reset(1);
  wchar_t *wide = nullptr;
  EXPECT_EQ(scan(L"abc", L"%mls", &wide), EOF);
  EXPECT_EQ(wide, static_cast<wchar_t *>(nullptr));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
  EXPECT_EQ(live, size_t(0));
  reset(2);
  wchar_t *second = nullptr;
  EXPECT_EQ(scan(L"abc def", L"%mls %mls", &wide, &second), 1);
  ASSERT_NE(wide, nullptr);
  EXPECT_EQ(wide[0], L'a');
  EXPECT_EQ(wide[3], wchar_t(0));
  EXPECT_EQ(second, static_cast<wchar_t *>(nullptr));
  EXPECT_EQ(live, size_t(1));
  scan_free(wide);
  EXPECT_EQ(live, size_t(0));

  reset(0);
  char *bytes = nullptr;
  const wchar_t invalid[] = {L'a', wchar_t(0xd800), 0};
  EXPECT_EQ(scan(invalid, L"%ms", &bytes), EOF);
  EXPECT_EQ(bytes, static_cast<char *>(nullptr));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EILSEQ);
  EXPECT_EQ(live, size_t(0));
}

#if defined(__cpp_exceptions)
TEST(LlvmLibcScanfAllocationTest, UnwindReleasesUnpublishedStorage) {
  using namespace LIBC_NAMESPACE::scanf_core;
  struct Source : Reader<Source> {
    size_t reads = 0;
    int getc() {
      if (++reads == 40)
        throw 7;
      return 'a';
    }
    void ungetc(int) {}
  } source;
  reset(0);
  char *out = nullptr;
  FormatSection section{};
  section.conv_name = 's';
  section.flags = ALLOCATE;
  section.output_ptr = &out;
  bool caught = false;
  try {
    convert_string(static_cast<Reader<Source> *>(&source), section);
  } catch (int value) {
    caught = value == 7;
  }
  EXPECT_TRUE(caught);
  EXPECT_EQ(out, static_cast<char *>(nullptr));
  EXPECT_EQ(live, size_t(0));
}
#endif

#ifndef LIBC_COPT_SCANF_DISABLE_FLOAT
TEST(LlvmLibcScanfAllocationTest, FloatTokenFailuresAreInputErrors) {
  char input[100];
  wchar_t wide[100];
  input[0] = '1';
  input[1] = '.';
  wide[0] = L'1';
  wide[1] = L'.';
  for (size_t i = 2; i < 99; ++i) {
    input[i] = '0';
    wide[i] = L'0';
  }
  input[99] = 0;
  wide[99] = 0;
  reset(1);
  double value = 17;
  EXPECT_EQ(scan(input, "%lf", &value), EOF);
  EXPECT_TRUE(value == 17.0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
  EXPECT_EQ(live, size_t(0));
  reset(1);
  EXPECT_EQ(scan(wide, L"%lf", &value), EOF);
  EXPECT_TRUE(value == 17.0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
  EXPECT_EQ(live, size_t(0));
  reset(1);
  int count = -1;
  EXPECT_EQ(scan(input, "%n%lf", &count, &value), 0);
  EXPECT_EQ(count, 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOMEM);
}
#endif
