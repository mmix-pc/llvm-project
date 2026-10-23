//===-- Tests for replacing memory stream associations --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/stdio_macros.h"
#include "src/__support/File/file.h"
#include "src/stdio/fclose.h"
#include "src/stdio/fflush.h"
#include "src/stdio/fileno.h"
#include "src/stdio/flockfile.h"
#include "src/stdio/fmemopen.h"
#include "src/stdio/fread.h"
#include "src/stdio/freopen.h"
#include "src/stdio/ftell.h"
#include "src/stdio/funlockfile.h"
#include "src/stdio/fwrite.h"
#include "src/stdio/open_memstream.h"
#include "src/wchar/fputws.h"
#include "src/wchar/fwide.h"
#include "src/wchar/open_wmemstream.h"
#include "test/UnitTest/ErrnoCheckingTest.h"

namespace LIBC = LIBC_NAMESPACE;
using LlvmLibcMemoryReopenTest = LIBC::testing::ErrnoCheckingTest;

static size_t registered_files() {
  size_t count = 0;
  LIBC::File::lock_list();
  for (auto *f = LIBC::File::get_first_file(); f; f = f->get_next())
    ++count;
  LIBC::File::unlock_list();
  return count;
}

TEST_F(LlvmLibcMemoryReopenTest, ReplacesAllMemoryBackendsInPlace) {
  const auto path = libc_make_test_file_path("memory-reopen.test");
  for (unsigned kind = 0; kind < 3; ++kind) {
    size_t baseline = registered_files();
    char fixed[16] = {};
    char *bytes = nullptr;
    wchar_t *wide = nullptr;
    size_t size = 0;
    FILE *f = kind == 0   ? LIBC::fmemopen(fixed, sizeof(fixed), "w+")
              : kind == 1 ? LIBC::open_memstream(&bytes, &size)
                          : LIBC::open_wmemstream(&wide, &size);
    ASSERT_NE(f, nullptr);
    if (kind == 2)
      ASSERT_GE(LIBC::fputws(L"old", f), 0);
    else
      ASSERT_EQ(LIBC::fwrite("old", 1, 3, f), size_t(3));

    LIBC::flockfile(f);
    FILE *replacement = LIBC::freopen(path, "w+", f);
    // Release the retained lock only after successful replacement.
    if (replacement)
      LIBC::funlockfile(replacement);
    ASSERT_EQ(replacement, f);
    EXPECT_EQ(registered_files(), baseline + 1);
    EXPECT_EQ(LIBC::fwide(f, 0), 0);
    EXPECT_GE(LIBC::fileno(f), 0);
    EXPECT_EQ(LIBC::ftell(f), 0L);
    if (kind == 0) {
      EXPECT_STREQ(fixed, "old");
    } else {
      EXPECT_EQ(size, size_t(3));
      if (kind == 1) {
        EXPECT_STREQ(bytes, "old");
        ::free(bytes);
      } else {
        EXPECT_EQ(wide[0], L'o');
        EXPECT_EQ(wide[3], L'\0');
        ::free(wide);
      }
    }
    // The old memory is now caller-owned; no subsequent flush may access it.
    ASSERT_EQ(LIBC::fwrite("new", 1, 3, f), size_t(3));
    ASSERT_EQ(LIBC::fflush(nullptr), 0);
    EXPECT_EQ(LIBC::ftell(f), 3L);
    ASSERT_EQ(LIBC::freopen(nullptr, "r", f), f);
    char input[4] = {};
    ASSERT_EQ(LIBC::fread(input, 1, 3, f), size_t(3));
    EXPECT_STREQ(input, "new");

    ASSERT_EQ(LIBC::freopen(path, "w", f), f);
    EXPECT_EQ(LIBC::fwide(f, 0), 0);
    ASSERT_GE(LIBC::fputws(L"x", f), 0);
    ASSERT_EQ(LIBC::fclose(f), 0);
    EXPECT_EQ(registered_files(), baseline);
  }
}

TEST_F(LlvmLibcMemoryReopenTest, NullPathConsumesAndPublishesMemoryStream) {
  size_t baseline = registered_files();
  char *bytes;
  size_t size;
  FILE *f = LIBC::open_memstream(&bytes, &size);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(LIBC::fwrite("old", 1, 3, f), size_t(3));
  EXPECT_EQ(LIBC::freopen(nullptr, "w", f), static_cast<FILE *>(nullptr));
  ASSERT_ERRNO_EQ(EBADF);
  EXPECT_EQ(registered_files(), baseline);
  // fflush was attempted before the rejected conversion consumed the stream.
  EXPECT_EQ(size, size_t(3));
  EXPECT_STREQ(bytes, "old");
  ::free(bytes);
}

TEST_F(LlvmLibcMemoryReopenTest, NamedFailureConsumesAndPublishesMemoryStream) {
  size_t baseline = registered_files();
  wchar_t *wide;
  size_t size;
  FILE *f = LIBC::open_wmemstream(&wide, &size);
  ASSERT_NE(f, nullptr);
  ASSERT_GE(LIBC::fputws(L"old", f), 0);
  EXPECT_EQ(LIBC::freopen("/dev/null/missing", "w", f),
            static_cast<FILE *>(nullptr));
  ASSERT_ERRNO_EQ(ENOTDIR);
  EXPECT_EQ(registered_files(), baseline);
  EXPECT_EQ(size, size_t(3));
  EXPECT_EQ(wide[0], L'o');
  EXPECT_EQ(wide[3], L'\0');
  ::free(wide);
}
