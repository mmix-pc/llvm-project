//===-- Tests for stream positions ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "hdr/wchar_macros.h"
#include "src/__support/File/file.h"
#include "src/stdio/fclose.h"
#include "src/stdio/fdopen.h"
#include "src/stdio/feof.h"
#include "src/stdio/fgetpos.h"
#include "src/stdio/fopen.h"
#include "src/stdio/fseek.h"
#include "src/stdio/fsetpos.h"
#include "src/unistd/close.h"
#include "src/unistd/pipe.h"
#include "src/wchar/fgetwc.h"
#include "src/wchar/fputwc.h"
#include "src/wchar/ungetwc.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/Test.h"
#include "test/src/locale/LocaleGuard.h"

TEST(LlvmLibcFposTest, WideBufferedPosition) {
  LocaleGuard locale;
  auto name = libc_make_test_file_path("fpos-wide.test");
  ::FILE *file = LIBC_NAMESPACE::fopen(name, "w+");
  ASSERT_NE(file, nullptr);
  ASSERT_EQ(LIBC_NAMESPACE::fputwc(L'A', file), wint_t(L'A'));
  fpos_t saved;
  ASSERT_EQ(LIBC_NAMESPACE::fgetpos(file, &saved), 0);
  ASSERT_EQ(LIBC_NAMESPACE::fputwc(0x20ac, file), wint_t(0x20ac));
  ASSERT_EQ(LIBC_NAMESPACE::fsetpos(file, &saved), 0);
  ASSERT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(0x20ac));
  ASSERT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(WEOF));
  ASSERT_NE(LIBC_NAMESPACE::feof(file), 0);
  ASSERT_EQ(LIBC_NAMESPACE::fsetpos(file, &saved), 0);
  ASSERT_EQ(LIBC_NAMESPACE::feof(file), 0);
  ASSERT_EQ(LIBC_NAMESPACE::ungetwc(L'Z', file), wint_t(L'Z'));
  ASSERT_EQ(LIBC_NAMESPACE::fsetpos(file, &saved), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fgetwc(file), wint_t(0x20ac));
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}

TEST(LlvmLibcFposTest, ConversionStateSnapshot) {
  auto name = libc_make_test_file_path("fpos-state.test");
  ::FILE *file = LIBC_NAMESPACE::fopen(name, "w+");
  ASSERT_NE(file, nullptr);
  auto *internal = reinterpret_cast<LIBC_NAMESPACE::File *>(file);
  // Exercise the state transfer independently of the stateless UTF-8 encoding.
  LIBC_NAMESPACE::File::Position state{0, {0x20ac, 2, 3}};
  ASSERT_TRUE(internal->set_position(state).has_value());
  fpos_t saved;
  ASSERT_EQ(LIBC_NAMESPACE::fgetpos(file, &saved), 0);
  ASSERT_EQ(LIBC_NAMESPACE::fseek(file, 0, SEEK_SET), 0);
  ASSERT_EQ(LIBC_NAMESPACE::fsetpos(file, &saved), 0);
  auto restored = internal->get_position();
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(static_cast<unsigned>(restored.value().state.partial), 0x20acu);
  EXPECT_EQ(restored.value().state.bytes_stored, uint8_t(2));
  EXPECT_EQ(restored.value().state.total_bytes, uint8_t(3));
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
}

using LlvmLibcFposErrorTest = LIBC_NAMESPACE::testing::ErrnoCheckingTest;

TEST_F(LlvmLibcFposErrorTest, NonseekableStream) {
  int descriptors[2];
  ASSERT_EQ(LIBC_NAMESPACE::pipe(descriptors), 0);
  ::FILE *file = LIBC_NAMESPACE::fdopen(descriptors[0], "r");
  ASSERT_NE(file, nullptr);
  fpos_t saved{123, {}};
  EXPECT_EQ(LIBC_NAMESPACE::fgetpos(file, &saved), -1);
  ASSERT_ERRNO_EQ(ESPIPE);
  EXPECT_EQ(saved.__offset, off_t(123));
  EXPECT_EQ(LIBC_NAMESPACE::fsetpos(file, &saved), -1);
  ASSERT_ERRNO_EQ(ESPIPE);
  EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
  EXPECT_EQ(LIBC_NAMESPACE::close(descriptors[1]), 0);
}
