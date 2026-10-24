//===-- Stream extension state and backing-I/O tests
//-----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/File/file.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/fread.h"
#include "src/stdio/fwrite.h"
#include "src/stdio_ext/__fbufsize.h"
#include "src/stdio_ext/__flbf.h"
#include "src/stdio_ext/__fpending.h"
#include "src/stdio_ext/__fpurge.h"
#include "src/stdio_ext/__freadable.h"
#include "src/stdio_ext/__freading.h"
#include "src/stdio_ext/__fwritable.h"
#include "src/stdio_ext/__fwriting.h"
#include "src/stdio_ext/_flushlbf.h"
#include "test/UnitTest/Test.h"
#include "test/src/locale/LocaleGuard.h"

namespace LIBC_NAMESPACE_DECL {
FILE *stdin = nullptr;
FILE *stdout = nullptr;
FILE *stderr = nullptr;
} // namespace LIBC_NAMESPACE_DECL

using LIBC_NAMESPACE::ErrorOr;
using LIBC_NAMESPACE::File;
using LIBC_NAMESPACE::FileIOResult;
namespace {
class Stream : public File {
  uint8_t buffer[32]{};

public:
  char data[1024]{};
  size_t offset = 0, length = 0, limit = sizeof(data);
  unsigned writes = 0, reads = 0, seeks = 0, publications = 0;
  int failure = 0;
  bool seekable = true;
  static FileIOResult write_callback(File *base, const void *data,
                                     size_t size) {
    auto &f = *static_cast<Stream *>(base);
    ++f.writes;
    size_t count = size < f.limit ? size : f.limit;
    for (size_t i = 0; i < count; ++i)
      f.data[f.offset++] = static_cast<const char *>(data)[i];
    if (f.offset > f.length)
      f.length = f.offset;
    return {count, f.failure};
  }
  static FileIOResult read_callback(File *base, void *data, size_t size) {
    auto &f = *static_cast<Stream *>(base);
    ++f.reads;
    if (f.failure)
      return {0, f.failure};
    size_t count = size < f.length - f.offset ? size : f.length - f.offset;
    for (size_t i = 0; i < count; ++i)
      static_cast<char *>(data)[i] = f.data[f.offset++];
    return count;
  }
  static ErrorOr<off_t> seek_callback(File *base, off_t offset, int whence) {
    auto &f = *static_cast<Stream *>(base);
    ++f.seeks;
    if (!f.seekable)
      return LIBC_NAMESPACE::Error(ESPIPE);
    if (whence == SEEK_CUR)
      offset += f.offset;
    else if (whence == SEEK_END)
      offset += f.length;
    if (offset < 0 || static_cast<size_t>(offset) > sizeof(f.data))
      return LIBC_NAMESPACE::Error(EINVAL);
    f.offset = offset;
    return offset;
  }
  static int publish(File *base) {
    ++static_cast<Stream *>(base)->publications;
    return 0;
  }
  static int close_callback(File *) { return 0; }
  explicit Stream(const char *mode = "w+", int buffering = _IOFBF)
      : File(write_callback, read_callback, seek_callback, close_callback,
             buffer, sizeof(buffer), buffering, false, mode_flags(mode),
             publish) {}
  ~Stream() { close(false); }
  FILE *handle() { return reinterpret_cast<FILE *>(this); }
  void reopen(const char *mode) {
    reset_stream_state_unlocked(mode_flags(mode));
  }
};
} // namespace

TEST(LlvmLibcStreamExtensionsTest, AccessAndInitialDirection) {
  struct Entry {
    const char *mode;
    bool read, write, reading, writing;
  };
  Entry entries[] = {
      {"r", true, false, true, false},  {"w", false, true, false, true},
      {"a", false, true, false, true},  {"r+", true, true, false, false},
      {"w+", true, true, false, false}, {"a+", true, true, false, false}};
  for (const auto &entry : entries) {
    Stream f(entry.mode);
    EXPECT_EQ(bool(LIBC_NAMESPACE::__freadable(f.handle())), entry.read);
    EXPECT_EQ(bool(LIBC_NAMESPACE::__fwritable(f.handle())), entry.write);
    EXPECT_EQ(bool(LIBC_NAMESPACE::__freading(f.handle())), entry.reading);
    EXPECT_EQ(bool(LIBC_NAMESPACE::__fwriting(f.handle())), entry.writing);
    EXPECT_EQ(f.get_orientation(), File::Orientation::UNORIENTED);
    EXPECT_EQ(f.reads + f.writes + f.seeks, 0U);
  }
}

TEST(LlvmLibcStreamExtensionsTest, DirectionTransitions) {
  Stream f;
  char byte;
  EXPECT_EQ(LIBC_NAMESPACE::fwrite("", 1, 0, f.handle()), size_t(0));
  EXPECT_EQ(LIBC_NAMESPACE::fread(&byte, 1, 0, f.handle()), size_t(0));
  EXPECT_FALSE(f.writing());
  EXPECT_FALSE(f.reading());
  EXPECT_EQ(f.write("abc", 3).value, size_t(3));
  EXPECT_TRUE(f.writing());
  EXPECT_FALSE(f.reading());
  f.seekable = false;
  EXPECT_FALSE(f.seek(0, SEEK_SET).has_value());
  EXPECT_TRUE(f.writing());
  f.seekable = true;
  EXPECT_TRUE(f.seek(0, SEEK_SET).has_value());
  EXPECT_TRUE(f.reading());
  EXPECT_EQ(f.read(&byte, 1).value, size_t(1));
  EXPECT_EQ(byte, 'a');
  f.reopen("r+");
  EXPECT_FALSE(f.reading());
  EXPECT_FALSE(f.writing());
  f.failure = EIO;
  EXPECT_EQ(f.read(&byte, 1).error, EIO);
  EXPECT_TRUE(f.reading());
  f.reopen("r");
  EXPECT_EQ(f.write("x", 1).error, EBADF);
  EXPECT_FALSE(f.writing());
}

TEST(LlvmLibcStreamExtensionsTest, ByteBufferAndShortWrite) {
  Stream f;
  EXPECT_EQ(LIBC_NAMESPACE::__fbufsize(f.handle()), size_t(32));
  EXPECT_EQ(LIBC_NAMESPACE::__flbf(f.handle()), 0);
  EXPECT_EQ(f.write("abcd", 4).value, size_t(4));
  EXPECT_EQ(LIBC_NAMESPACE::__fpending(f.handle()), size_t(4));
  f.limit = 2;
  f.failure = EIO;
  EXPECT_EQ(f.flush(), EIO);
  EXPECT_EQ(LIBC_NAMESPACE::__fpending(f.handle()), size_t(2));
  EXPECT_EQ(f.tell().value(), off_t(4));
  f.limit = sizeof(f.data);
  f.failure = 0;
  EXPECT_EQ(f.flush(), 0);
  EXPECT_EQ(LIBC_NAMESPACE::__fpending(f.handle()), size_t(0));
  EXPECT_EQ(f.length, size_t(4));
  EXPECT_EQ(f.data[2], 'c');
  EXPECT_EQ(f.data[3], 'd');
}

TEST(LlvmLibcStreamExtensionsTest, OutputAfterInputEOF) {
  Stream f;
  f.data[0] = 'a';
  f.data[1] = 'b';
  f.length = 2;
  char bytes[8];
  EXPECT_EQ(f.read(bytes, sizeof(bytes)).value, size_t(2));
  EXPECT_TRUE(f.iseof());
  EXPECT_EQ(f.write("c", 1).value, size_t(1));
  EXPECT_TRUE(f.writing());
  EXPECT_EQ(f.pending_output(), size_t(1));
  EXPECT_EQ(f.flush(), 0);
  EXPECT_EQ(f.length, size_t(3));
  EXPECT_EQ(f.data[2], 'c');
}

TEST(LlvmLibcStreamExtensionsTest, BufferSelection) {
  Stream f;
  EXPECT_EQ(f.set_buffer(nullptr, 0, _IONBF), 0);
  EXPECT_EQ(f.buffer_capacity(), size_t(0));
  EXPECT_EQ(f.write("abc", 3).value, size_t(3));
  EXPECT_EQ(f.pending_output(), size_t(0));
  Stream line("w", _IOLBF);
  EXPECT_NE(LIBC_NAMESPACE::__flbf(line.handle()), 0);
  EXPECT_EQ(line.write("ab\nc", 4).value, size_t(4));
  EXPECT_EQ(line.pending_output(), size_t(1));
  EXPECT_EQ(line.length, size_t(3));
  Stream allocated;
  EXPECT_EQ(allocated.set_buffer(nullptr, 64, _IOFBF), 0);
  EXPECT_EQ(allocated.buffer_capacity(), size_t(64));
}

TEST(LlvmLibcStreamExtensionsTest, WideCharactersAndPartialEncoding) {
  LocaleGuard locale;
  Stream f;
  EXPECT_EQ(f.try_set_orientation(File::Orientation::WIDE),
            File::Orientation::WIDE);
  EXPECT_EQ(f.buffer_capacity(), size_t(0));
  const wchar_t text[] = {L'a', 0x20ac, L'b'};
  EXPECT_EQ(f.write(text, 3).value, size_t(3));
  EXPECT_EQ(LIBC_NAMESPACE::__fbufsize(f.handle()), size_t(8));
  EXPECT_EQ(LIBC_NAMESPACE::__fpending(f.handle()), size_t(3));
  EXPECT_EQ(f.tell().value(), off_t(5));
  f.limit = 2;
  f.failure = EIO;
  EXPECT_EQ(f.flush(), EIO); // 'a' and the first byte of the Euro sign.
  EXPECT_EQ(f.pending_output(), size_t(2));
  EXPECT_EQ(f.tell().value(), off_t(5));
  f.limit = 1;
  EXPECT_EQ(f.flush(), EIO);
  EXPECT_EQ(f.pending_output(), size_t(2));
  f.limit = sizeof(f.data);
  f.failure = 0;
  EXPECT_EQ(f.flush(), 0);
  EXPECT_EQ(f.pending_output(), size_t(0));
  EXPECT_EQ(f.length, size_t(5));
  const unsigned char expected[] = {'a', 0xe2, 0x82, 0xac, 'b'};
  for (size_t i = 0; i < 5; ++i)
    EXPECT_EQ(static_cast<unsigned char>(f.data[i]), expected[i]);
}

TEST(LlvmLibcStreamExtensionsTest, WideBoundariesAndErrors) {
  LocaleGuard locale;
  Stream f("w", _IOLBF);
  EXPECT_EQ(f.write(L"123456789\nx", 11).value, size_t(11));
  EXPECT_EQ(f.pending_output(), size_t(1));
  EXPECT_EQ(f.length, size_t(10));
  wchar_t invalid = 0xd800;
  EXPECT_EQ(f.write(&invalid, 1).error, EILSEQ);
  EXPECT_EQ(f.pending_output(), size_t(1));
  EXPECT_TRUE(f.seek(0, SEEK_SET).has_value());
  EXPECT_EQ(f.pending_output(), size_t(0));
  Stream unbuffered("w", _IONBF);
  EXPECT_EQ(unbuffered.write(L"abc", 3).value, size_t(3));
  EXPECT_EQ(unbuffered.buffer_capacity(), size_t(0));
  EXPECT_EQ(unbuffered.pending_output(), size_t(0));
}

TEST(LlvmLibcStreamExtensionsTest, PurgeOutputDoesNotPublish) {
  Stream f;
  EXPECT_EQ(f.write("abcd", 4).value, size_t(4));
  f.limit = 1;
  f.failure = EIO;
  EXPECT_EQ(f.flush(), EIO);
  unsigned writes = f.writes, seeks = f.seeks, publications = f.publications;
  LIBC_NAMESPACE::__fpurge(f.handle());
  EXPECT_EQ(f.writes, writes);
  EXPECT_EQ(f.seeks, seeks);
  EXPECT_EQ(f.publications, publications);
  EXPECT_TRUE(f.error());
  EXPECT_TRUE(f.writing());
  EXPECT_EQ(f.pending_output(), size_t(0));
  EXPECT_EQ(f.tell().value(), off_t(1));
  EXPECT_EQ(f.flush(), 0);
  EXPECT_EQ(f.writes, writes);
}

TEST(LlvmLibcStreamExtensionsTest, PurgeInputAndPushback) {
  Stream f;
  EXPECT_EQ(f.write("abcdef", 6).value, size_t(6));
  EXPECT_TRUE(f.seek(0, SEEK_SET).has_value());
  char c;
  EXPECT_EQ(f.read(&c, 1).value, size_t(1));
  EXPECT_EQ(f.ungetc('x'), int('x'));
  unsigned seeks = f.seeks;
  f.seekable = false;
  LIBC_NAMESPACE::__fpurge(f.handle());
  EXPECT_EQ(f.seeks, seeks);
  EXPECT_TRUE(f.reading());
  EXPECT_EQ(f.read(&c, 1).value, size_t(0));
  EXPECT_TRUE(f.iseof());
  LIBC_NAMESPACE::__fpurge(f.handle());
  EXPECT_TRUE(f.iseof());
}

TEST(LlvmLibcStreamExtensionsTest, PurgeWideInputAndPartialOutput) {
  LocaleGuard locale;
  Stream f;
  EXPECT_EQ(f.write(L"a\u20acb", 3).value, size_t(3));
  f.limit = 2;
  f.failure = EIO;
  EXPECT_EQ(f.flush(), EIO);
  LIBC_NAMESPACE::__fpurge(f.handle());
  EXPECT_EQ(f.pending_output(), size_t(0));
  EXPECT_EQ(f.get_orientation(), File::Orientation::WIDE);
  EXPECT_EQ(f.tell().value(), off_t(2));
  EXPECT_EQ(f.flush(), 0);
  EXPECT_EQ(f.length, size_t(2));
  f.limit = sizeof(f.data);
  f.failure = 0;
  EXPECT_TRUE(f.seek(0, SEEK_SET).has_value());
  wchar_t wc;
  EXPECT_EQ(f.read(&wc, 1).value, size_t(1));
  EXPECT_EQ(f.ungetwc(0x20ac).value(), wint_t(0x20ac));
  unsigned seeks = f.seeks;
  LIBC_NAMESPACE::__fpurge(f.handle());
  EXPECT_EQ(f.seeks, seeks);
  EXPECT_EQ(f.read(&wc, 1).value, size_t(0));
}

TEST(LlvmLibcStreamExtensionsTest, GlobalSelectionDeduplicationAndErrors) {
  Stream line("w", _IOLBF), full, input("r", _IOLBF), failed("w", _IOLBF);
  line.write("ab", 2);
  full.write("cd", 2);
  failed.write("ef", 2);
  failed.limit = 1;
  failed.failure = EIO;
  File::add_file(&line);
  File::add_file(&full);
  File::add_file(&input);
  File::add_file(&failed);
  LIBC_NAMESPACE::stdout = line.handle();
  LIBC_NAMESPACE::stderr = line.handle();
  LIBC_NAMESPACE::libc_errno = 0;
  LIBC_NAMESPACE::_flushlbf();
  EXPECT_EQ(line.writes, 1U);
  EXPECT_EQ(line.publications, 1U);
  EXPECT_EQ(full.writes, 0U);
  EXPECT_EQ(full.pending_output(), size_t(2));
  EXPECT_EQ(input.publications, 0U);
  EXPECT_EQ(failed.pending_output(), size_t(1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EIO);
  EXPECT_TRUE(failed.error());
  LIBC_NAMESPACE::stdout = LIBC_NAMESPACE::stderr = nullptr;
}

TEST(LlvmLibcStreamExtensionsTest, PositiveShortWritesCompletePendingOutput) {
  LocaleGuard locale;
  Stream bytes, wide;
  bytes.limit = wide.limit = 1;
  EXPECT_EQ(bytes.write("abc", 3).value, size_t(3));
  EXPECT_EQ(wide.write(L"a\u20ac", 2).value, size_t(2));
  EXPECT_EQ(bytes.flush(), 0);
  EXPECT_EQ(wide.flush(), 0);
  EXPECT_EQ(bytes.pending_output(), size_t(0));
  EXPECT_EQ(wide.pending_output(), size_t(0));
  EXPECT_EQ(bytes.length, size_t(3));
  EXPECT_EQ(wide.length, size_t(4));
  EXPECT_EQ(bytes.writes, 3U);
  EXPECT_EQ(wide.writes, 4U);
  EXPECT_FALSE(bytes.error());
  EXPECT_FALSE(wide.error());
}
