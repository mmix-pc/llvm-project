//===-- Memory stream allocation failure tests ----------------------------===//
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
#include "src/__support/File/file.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include "src/stdio/clearerr.h"
#include "src/stdio/fclose.h"
#include "src/stdio/ferror.h"
#include "src/stdio/fflush.h"
#include "src/stdio/fseek.h"
#include "src/stdio/ftell.h"
#include "src/stdio/fwrite.h"
#include "src/stdio/open_memstream.h"
#include "src/stdio/setvbuf.h"
#include "src/wchar/fputwc.h"
#include "src/wchar/open_wmemstream.h"
#include "test/UnitTest/ErrnoCheckingTest.h"

// Only the production memory streams participate in global flush traversal.
namespace LIBC_NAMESPACE_DECL {
FILE *stdin = nullptr;
FILE *stdout = nullptr;
FILE *stderr = nullptr;
} // namespace LIBC_NAMESPACE_DECL

namespace {
bool fail_reallocation = false;
bool force_move = false;
size_t allocation_size;
unsigned moves;
}

extern "C" void *memory_stream_test_realloc(void *pointer,
                                            size_t size) noexcept {
  if (fail_reallocation)
    return nullptr;
  if (force_move) {
    void *replacement = ::malloc(size);
    if (!replacement)
      return nullptr;
    LIBC_NAMESPACE::inline_memcpy(replacement, pointer,
                                 allocation_size < size ? allocation_size : size);
    ::free(pointer);
    allocation_size = size;
    ++moves;
    return replacement;
  }
  return ::realloc(pointer, size);
}

// Parse host allocator declarations before redirecting the production call.
#define realloc memory_stream_test_realloc
#include "src/stdio/open_memstream.cpp"
#include "src/wchar/open_wmemstream.cpp"
#undef realloc

using LlvmLibcMemstreamAllocTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;
namespace LIBC = LIBC_NAMESPACE;

TEST_F(LlvmLibcMemstreamAllocTest, FailedGrowthCanBeRetried) {
  char *memory;
  size_t length;
  FILE *f = LIBC::open_memstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(LIBC::setvbuf(f, nullptr, _IONBF, 0), 0);
  ASSERT_EQ(LIBC::fwrite("abc", 1, 3, f), size_t(3));
  char *saved = memory;
  fail_reallocation = true;
  EXPECT_EQ(LIBC::fwrite("defgh", 1, 5, f), size_t(0));
  fail_reallocation = false;
  ASSERT_ERRNO_EQ(ENOMEM);
  EXPECT_NE(LIBC::ferror(f), 0);
  EXPECT_EQ(LIBC::ftell(f), 3L);
  EXPECT_EQ(memory, saved);
  EXPECT_EQ(length, size_t(3));
  EXPECT_STREQ(memory, "abc");
  LIBC::clearerr(f);
  ASSERT_EQ(LIBC::fwrite("defgh", 1, 5, f), size_t(5));
  ASSERT_EQ(LIBC::fclose(f), 0);
  EXPECT_EQ(length, size_t(8));
  EXPECT_STREQ(memory, "abcdefgh");
  ::free(memory);
}

TEST_F(LlvmLibcMemstreamAllocTest, BufferedFlushFailureRetainsPendingData) {
  char *memory;
  size_t length;
  FILE *f = LIBC::open_memstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(LIBC::fwrite("abc", 1, 3, f), size_t(3));
  fail_reallocation = true;
  EXPECT_EQ(LIBC::fflush(f), EOF);
  fail_reallocation = false;
  ASSERT_ERRNO_EQ(ENOMEM);
  EXPECT_EQ(length, size_t(0));
  EXPECT_STREQ(memory, "");
  EXPECT_EQ(LIBC::ftell(f), 3L);
  LIBC::clearerr(f);
  ASSERT_EQ(LIBC::fflush(f), 0);
  EXPECT_EQ(length, size_t(3));
  EXPECT_STREQ(memory, "abc");
  ASSERT_EQ(LIBC::fclose(f), 0);
  ::free(memory);
}

TEST_F(LlvmLibcMemstreamAllocTest, FailedCloseTransfersLastValidAllocation) {
  char *memory;
  size_t length;
  FILE *f = LIBC::open_memstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(LIBC::fwrite("abc", 1, 3, f), size_t(3));
  ASSERT_EQ(LIBC::fflush(f), 0);
  ASSERT_EQ(LIBC::fwrite("defgh", 1, 5, f), size_t(5));
  fail_reallocation = true;
  EXPECT_EQ(LIBC::fclose(f), EOF);
  fail_reallocation = false;
  ASSERT_ERRNO_EQ(ENOMEM);
  EXPECT_EQ(length, size_t(3));
  EXPECT_STREQ(memory, "abc");
  // The closed stream cannot remain in the global list after either error.
  EXPECT_EQ(LIBC::fflush(nullptr), 0);
  ::free(memory);
}

TEST_F(LlvmLibcMemstreamAllocTest, WideGrowthFailurePreservesPublishedStorage) {
  wchar_t *memory;
  size_t length;
  FILE *f = LIBC::open_wmemstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(LIBC::fputwc(L'x', f), wint_t(L'x'));
  ASSERT_EQ(LIBC::fflush(f), 0);
  wchar_t *saved = memory;
  fail_reallocation = true;
  EXPECT_EQ(LIBC::fputwc(L'y', f), wint_t(-1));
  fail_reallocation = false;
  ASSERT_ERRNO_EQ(ENOMEM);
  EXPECT_NE(LIBC::ferror(f), 0);
  EXPECT_EQ(LIBC::ftell(f), 1L);
  EXPECT_EQ(memory, saved);
  EXPECT_EQ(length, size_t(1));
  EXPECT_EQ(memory[0], L'x');
  EXPECT_EQ(memory[1], L'\0');
  LIBC::clearerr(f);
  ASSERT_EQ(LIBC::fputwc(L'y', f), wint_t(L'y'));
  ASSERT_EQ(LIBC::fclose(f), 0);
  EXPECT_EQ(length, size_t(2));
  EXPECT_EQ(memory[1], L'y');
  EXPECT_EQ(memory[2], L'\0');
  EXPECT_EQ(LIBC::fflush(nullptr), 0);
  ::free(memory);
}

TEST_F(LlvmLibcMemstreamAllocTest, MovingGrowthRepublishesByteAndWideStorage) {
  char *bytes = nullptr;
  size_t length = 0;
  FILE *file = LIBC::open_memstream(&bytes, &length);
  ASSERT_NE(file, nullptr);
  allocation_size = 1;
  moves = 0;
  force_move = true;
  size_t count = LIBC::fwrite("abc", 1, 3, file);
  int result = LIBC::fflush(file);
  force_move = false;
  ASSERT_EQ(count, size_t(3));
  ASSERT_EQ(result, 0);
  EXPECT_EQ(moves, 1U);
  uintptr_t old_address = reinterpret_cast<uintptr_t>(bytes);
  force_move = true;
  count = LIBC::fwrite("defgh", 1, 5, file);
  result = LIBC::fflush(file);
  force_move = false;
  ASSERT_EQ(count, size_t(5));
  ASSERT_EQ(result, 0);
  EXPECT_EQ(moves, 2U);
  EXPECT_NE(reinterpret_cast<uintptr_t>(bytes), old_address);
  EXPECT_EQ(length, size_t(8));
  EXPECT_STREQ(bytes, "abcdefgh");
  ASSERT_EQ(LIBC::fclose(file), 0);
  ::free(bytes);

  wchar_t *wide = nullptr;
  file = LIBC::open_wmemstream(&wide, &length);
  ASSERT_NE(file, nullptr);
  allocation_size = sizeof(wchar_t);
  moves = 0;
  force_move = true;
  wint_t character = LIBC::fputwc(L'x', file);
  result = LIBC::fflush(file);
  force_move = false;
  ASSERT_EQ(character, wint_t(L'x'));
  ASSERT_EQ(result, 0);
  old_address = reinterpret_cast<uintptr_t>(wide);
  force_move = true;
  character = LIBC::fputwc(L'y', file);
  result = LIBC::fflush(file);
  force_move = false;
  ASSERT_EQ(character, wint_t(L'y'));
  ASSERT_EQ(result, 0);
  EXPECT_EQ(moves, 2U);
  EXPECT_NE(reinterpret_cast<uintptr_t>(wide), old_address);
  EXPECT_EQ(length, size_t(2));
  EXPECT_EQ(wide[0], L'x');
  EXPECT_EQ(wide[1], L'y');
  EXPECT_EQ(wide[2], L'\0');
  ASSERT_EQ(LIBC::fclose(file), 0);
  ::free(wide);
}
