//===-- Tests for input stream synchronization
//-----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "src/__support/File/file.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/fflush.h"
#include "test/UnitTest/Test.h"

namespace {
class InputFile : public LIBC_NAMESPACE::File {
  uint8_t buffer[8]{};

  static LIBC_NAMESPACE::FileIOResult read_input(File *base, void *out,
                                                 size_t count) {
    auto &self = *static_cast<InputFile *>(base);
    size_t available = 8 - self.offset;
    if (count > available)
      count = available;
    for (size_t i = 0; i < count; ++i)
      static_cast<char *>(out)[i] = static_cast<char>('a' + self.offset++);
    return count;
  }

  static LIBC_NAMESPACE::ErrorOr<off_t> seek_input(File *base, off_t delta,
                                                   int whence) {
    auto &self = *static_cast<InputFile *>(base);
    ++self.seek_calls;
    if (self.seek_error)
      return LIBC_NAMESPACE::Error(self.seek_error);
    if (whence != SEEK_CUR || delta < -self.offset || delta > 8 - self.offset)
      return LIBC_NAMESPACE::Error(EINVAL);
    self.offset += delta;
    return self.offset;
  }

public:
  off_t offset = 0;
  unsigned seek_calls = 0;
  int seek_error = 0;

  InputFile()
      : File(nullptr, read_input, seek_input, nullptr, buffer, sizeof(buffer),
             _IOFBF, false, mode_flags("r")) {}

  int flush_public() {
    return LIBC_NAMESPACE::fflush(reinterpret_cast<FILE *>(this));
  }
};
} // namespace

TEST(LlvmLibcInputFlushTest, SeekFailurePreservesUnreadDataAndPushback) {
  const int errors[] = {EIO, EINTR};
  for (int error : errors) {
    InputFile file;
    char bytes[2];
    ASSERT_EQ(file.read(bytes, sizeof(bytes)).value, sizeof(bytes));
    ASSERT_EQ(file.offset, off_t(8));
    ASSERT_EQ(file.ungetc('X'), int('X'));
    file.seek_error = error;
    LIBC_NAMESPACE::libc_errno = 0;
    EXPECT_EQ(file.flush_public(), EOF);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    EXPECT_TRUE(file.error());
    EXPECT_FALSE(file.iseof());
    EXPECT_EQ(file.offset, off_t(8));
    EXPECT_EQ(file.seek_calls, 1U);
    file.clearerr();
    ASSERT_EQ(file.read(bytes, sizeof(bytes)).value, sizeof(bytes));
    EXPECT_EQ(bytes[0], 'X');
    EXPECT_EQ(bytes[1], 'c');
    EXPECT_FALSE(file.error());
    EXPECT_FALSE(file.iseof());
  }
}

TEST(LlvmLibcInputFlushTest, RetrySynchronizesAndDiscardsPushback) {
  InputFile file;
  char bytes[2];
  ASSERT_EQ(file.read(bytes, sizeof(bytes)).value, sizeof(bytes));
  ASSERT_EQ(file.ungetc('X'), int('X'));
  file.seek_error = EIO;
  EXPECT_EQ(file.flush_public(), EOF);
  file.clearerr();
  file.seek_error = 0;
  EXPECT_EQ(file.flush_public(), 0);
  EXPECT_EQ(file.offset, off_t(1));
  EXPECT_EQ(file.seek_calls, 2U);
  EXPECT_FALSE(file.error());
  ASSERT_EQ(file.read(bytes, 1).value, size_t(1));
  EXPECT_EQ(bytes[0], 'b');
}

TEST(LlvmLibcInputFlushTest, NonseekableBoundaryIsRetained) {
  InputFile file;
  char byte;
  ASSERT_EQ(file.read(&byte, 1).value, size_t(1));
  file.seek_error = ESPIPE;
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(file.flush_public(), 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_EQ(file.seek_calls, 1U);
  EXPECT_FALSE(file.error());
}

TEST(LlvmLibcInputFlushTest, SeekFailurePreservesWidePushback) {
  InputFile file;
  wchar_t wc;
  ASSERT_EQ(file.read(&wc, 1).value, size_t(1));
  EXPECT_EQ(wc, L'a');
  ASSERT_TRUE(file.ungetwc(L'X').has_value());
  file.seek_error = EINTR;
  EXPECT_EQ(file.flush_public(), EOF);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINTR);
  EXPECT_TRUE(file.error());
  EXPECT_FALSE(file.iseof());
  file.clearerr();
  ASSERT_EQ(file.read(&wc, 1).value, size_t(1));
  EXPECT_EQ(wc, L'X');
  ASSERT_EQ(file.read(&wc, 1).value, size_t(1));
  EXPECT_EQ(wc, L'b');
}
