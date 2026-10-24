//===-- Unittests for platform independent file class ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/types/size_t.h"
#include "hdr/wchar_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/CPP/new.h"
#include "src/__support/File/file.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/error_or.h"
#include "test/UnitTest/MemoryMatcher.h"
#include "test/UnitTest/Test.h"
#include "test/src/locale/LocaleGuard.h"

using ModeFlags = LIBC_NAMESPACE::File::ModeFlags;
using MemoryView = LIBC_NAMESPACE::testing::MemoryView;
using LIBC_NAMESPACE::ErrorOr;
using LIBC_NAMESPACE::File;
using LIBC_NAMESPACE::FileIOResult;

class StringFile : public File {
  static constexpr size_t SIZE = 512;
  size_t pos;
  char str[SIZE] = {0};
  size_t eof_marker;
  bool write_append;

  static FileIOResult str_read(LIBC_NAMESPACE::File *f, void *data, size_t len);
  static FileIOResult str_write(LIBC_NAMESPACE::File *f, const void *data,
                                size_t len);
  static ErrorOr<off_t> str_seek(LIBC_NAMESPACE::File *f, off_t offset,
                                 int whence);
  static int str_close(LIBC_NAMESPACE::File *f) {
    delete reinterpret_cast<StringFile *>(f);
    return 0;
  }

public:
  explicit StringFile(char *buffer, size_t buflen, int bufmode, bool owned,
                      ModeFlags modeflags)
      : LIBC_NAMESPACE::File(&str_write, &str_read, &str_seek, &str_close,
                             reinterpret_cast<uint8_t *>(buffer), buflen,
                             bufmode, owned, modeflags),
        pos(0), eof_marker(0), write_append(false) {
    if (modeflags &
        static_cast<ModeFlags>(LIBC_NAMESPACE::File::OpenMode::APPEND))
      write_append = true;
  }

  void reset() { pos = 0; }
  size_t get_pos() const { return pos; }
  char *get_str() { return str; }

  // Use this method to prefill the file.
  void reset_and_fill(const char *data, size_t len) {
    size_t i;
    for (i = 0; i < len && i < SIZE; ++i) {
      str[i] = data[i];
    }
    pos = 0;
    eof_marker = i;
  }
};

FileIOResult StringFile::str_read(LIBC_NAMESPACE::File *f, void *data,
                                  size_t len) {
  StringFile *sf = static_cast<StringFile *>(f);
  if (sf->pos >= sf->eof_marker)
    return 0;
  size_t i = 0;
  for (i = 0; i < len; ++i)
    reinterpret_cast<char *>(data)[i] = sf->str[sf->pos + i];
  sf->pos += i;
  return i;
}

FileIOResult StringFile::str_write(LIBC_NAMESPACE::File *f, const void *data,
                                   size_t len) {
  StringFile *sf = static_cast<StringFile *>(f);
  if (sf->write_append)
    sf->pos = sf->eof_marker;
  if (sf->pos >= SIZE)
    return 0;
  size_t i = 0;
  for (i = 0; i < len && sf->pos < SIZE; ++i, ++sf->pos)
    sf->str[sf->pos] = reinterpret_cast<const char *>(data)[i];
  // Move the eof marker if the data was written beyond the current eof marker.
  if (sf->pos > sf->eof_marker)
    sf->eof_marker = sf->pos;
  return i;
}

ErrorOr<off_t> StringFile::str_seek(LIBC_NAMESPACE::File *f, off_t offset,
                                    int whence) {
  StringFile *sf = static_cast<StringFile *>(f);
  if (whence == SEEK_SET)
    sf->pos = offset;
  if (whence == SEEK_CUR)
    sf->pos += offset;
  if (whence == SEEK_END)
    sf->pos = SIZE + offset;
  return sf->pos;
}

StringFile *new_string_file(char *buffer, size_t buflen, int bufmode,
                            bool owned, const char *mode) {
  LIBC_NAMESPACE::AllocChecker ac;
  // We will just assume the allocation succeeds. We cannot test anything
  // otherwise.
  return new (ac) StringFile(buffer, buflen, bufmode, owned,
                             LIBC_NAMESPACE::File::mode_flags(mode));
}

TEST(LlvmLibcFileTest, ForkOwner) {
  char buffer[8];
  StringFile file(buffer, sizeof(buffer), _IOFBF, false, File::mode_flags("w"));
  ASSERT_TRUE(file.try_lock());
  ASSERT_TRUE(file.try_lock());
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
  const pid_t current = LIBC_NAMESPACE::internal::gettid();
  const pid_t previous = current == 1 ? 2 : 1;
  EXPECT_FALSE(file.rebind_owner_after_fork(previous, current));
  ASSERT_TRUE(file.rebind_owner_after_fork(current, previous));
  EXPECT_FALSE(file.try_lock());
  ASSERT_TRUE(file.rebind_owner_after_fork(previous, current));
#endif
  file.unlock();
  file.unlock();
}

TEST(LlvmLibcFileTest, WriteOnly) {
  LocaleGuard locale;
  const char data[] = "hello, file";
  constexpr size_t FILE_BUFFER_SIZE = sizeof(data) * 3 / 2;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "w");

  ASSERT_EQ(sizeof(data), f->write(data, sizeof(data)).value);
  EXPECT_EQ(f->get_pos(), size_t(0)); // Data is buffered in the file stream
  ASSERT_EQ(f->flush(), 0);
  EXPECT_EQ(f->get_pos(), sizeof(data)); // Data should now be available
  EXPECT_STREQ(f->get_str(), data);

  f->reset();
  ASSERT_EQ(f->get_pos(), size_t(0));
  ASSERT_EQ(sizeof(data), f->write(data, sizeof(data)).value);
  EXPECT_EQ(f->get_pos(), size_t(0)); // Data is buffered in the file stream
  // The second write should trigger a buffer flush.
  ASSERT_EQ(sizeof(data), f->write(data, sizeof(data)).value);
  EXPECT_GE(f->get_pos(), size_t(0));
  ASSERT_EQ(f->flush(), 0);
  EXPECT_EQ(f->get_pos(), 2 * sizeof(data));
  MemoryView src1("hello, file\0hello, file", sizeof(data) * 2),
      dst1(f->get_str(), sizeof(data) * 2);
  EXPECT_MEM_EQ(src1, dst1);

  char read_data[sizeof(data)];
  {
    // This is not a readable file.
    auto result = f->read(read_data, sizeof(data));
    EXPECT_EQ(result.value, size_t(0));
    EXPECT_TRUE(f->error());
    EXPECT_TRUE(result.has_error());
  }

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, RecursiveStreamLock) {
  LocaleGuard locale;
  char buffer[32];
  auto *f = new_string_file(buffer, sizeof(buffer), _IOFBF, false, "w");
  f->lock();
  f->lock();
  ASSERT_EQ(f->write("locked", 6).value, size_t(6));
  f->unlock();
  f->unlock();
  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, WriteLineBuffered) {
  LocaleGuard locale;
  const char data[] = "hello\n file";
  constexpr size_t FILE_BUFFER_SIZE = sizeof(data) * 3 / 2;

  char file_buffer_line[FILE_BUFFER_SIZE];
  char file_buffer_full[FILE_BUFFER_SIZE];

  StringFile *f_line =
      new_string_file(file_buffer_line, FILE_BUFFER_SIZE, _IOLBF, false, "w");
  // We also initialize a fully buffered file we'll do the same writes to for
  // comparison.
  StringFile *f_full =
      new_string_file(file_buffer_full, FILE_BUFFER_SIZE, _IOFBF, false, "w");

  ASSERT_EQ(sizeof(data), f_line->write(data, sizeof(data)).value);
  ASSERT_EQ(sizeof(data), f_full->write(data, sizeof(data)).value);

  EXPECT_EQ(f_line->get_pos(), size_t(6)); // buffer after the newline
  EXPECT_EQ(f_full->get_pos(), size_t(0)); // buffer all of data

  MemoryView src1("hello\n", 6), dst1(f_line->get_str(), 6);
  EXPECT_MEM_EQ(src1, dst1);

  // We can't check the data in f_full, since no data has been written.

  const char data2[] = "longer for an \n overflow";

  ASSERT_EQ(sizeof(data2), f_line->write(data2, sizeof(data2)).value);
  // The line buffer's initial contents should be " file\0"
  // Writing data2 should write up until the newline, even though that doesn't
  // all fit in the buffer.

  ASSERT_EQ(sizeof(data2), f_full->write(data2, sizeof(data2)).value);
  // The full buffer's initial contents should be "hello\n file\0"
  // Writing data2 should cause a flush of the buffer, as well as the remainder
  // to be written directly since it doesn't fit in the buffer.

  EXPECT_EQ(f_line->get_pos(), size_t(27));
  EXPECT_EQ(f_full->get_pos(), sizeof(data) + sizeof(data2));

  MemoryView src2("hello\n file\0longer for an \n", 27),
      dst2(f_line->get_str(), 27);
  EXPECT_MEM_EQ(src2, dst2);

  MemoryView src3("hello\n file\0longer for an \n overflow", 37),
      dst_full_final(f_full->get_str(), 37);
  EXPECT_MEM_EQ(src3, dst_full_final);

  ASSERT_EQ(f_line->flush(), 0);
  ASSERT_EQ(f_full->flush(), 0);

  EXPECT_EQ(f_line->get_pos(), sizeof(data) + sizeof(data2));
  MemoryView dst_line_final(f_line->get_str(), 37);
  EXPECT_MEM_EQ(src3, dst_line_final);
  EXPECT_MEM_EQ(src3, dst_full_final);

  ASSERT_EQ(f_line->close(), 0);
  ASSERT_EQ(f_full->close(), 0);
}

TEST(LlvmLibcFileTest, WriteUnbuffered) {
  LocaleGuard locale;
  const char data[] = "written immediately";
  constexpr size_t FILE_BUFFER_SIZE = sizeof(data) + 1;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IONBF, false, "w");

  ASSERT_EQ(sizeof(data), f->write(data, sizeof(data)).value);
  EXPECT_EQ(f->get_pos(),
            sizeof(data)); // no buffering means this is written immediately.
  EXPECT_STREQ(f->get_str(), data);

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, ReadOnly) {
  LocaleGuard locale;
  const char initial_content[] = "1234567890987654321";
  constexpr size_t FILE_BUFFER_SIZE = sizeof(initial_content);
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "r");
  f->reset_and_fill(initial_content, sizeof(initial_content));

  constexpr size_t READ_SIZE = sizeof(initial_content) / 2;
  char read_data[READ_SIZE];
  ASSERT_EQ(READ_SIZE, f->read(read_data, READ_SIZE).value);
  EXPECT_FALSE(f->iseof());
  // Reading less than file buffer worth will still read one
  // full buffer worth of data.
  EXPECT_STREQ(file_buffer, initial_content);
  EXPECT_STREQ(file_buffer, f->get_str());
  EXPECT_EQ(FILE_BUFFER_SIZE, f->get_pos());
  // The read data should match what was supposed to be read anyway.
  MemoryView src1(initial_content, READ_SIZE), dst1(read_data, READ_SIZE);
  EXPECT_MEM_EQ(src1, dst1);

  // Reading another buffer worth should read out everything in
  // the file.
  ASSERT_EQ(READ_SIZE, f->read(read_data, READ_SIZE).value);
  EXPECT_FALSE(f->iseof());
  MemoryView src2(initial_content + READ_SIZE, READ_SIZE),
      dst2(read_data, READ_SIZE);
  EXPECT_MEM_EQ(src2, dst2);

  // Another read should trigger an EOF.
  ASSERT_GT(READ_SIZE, f->read(read_data, READ_SIZE).value);
  EXPECT_TRUE(f->iseof());

  // Reset the pos to the beginning of the file which should allow
  // reading again.
  for (size_t i = 0; i < READ_SIZE; ++i)
    read_data[i] = 0;
  f->seek(0, SEEK_SET);
  ASSERT_EQ(READ_SIZE, f->read(read_data, READ_SIZE).value);
  MemoryView src3(initial_content, READ_SIZE), dst3(read_data, READ_SIZE);
  EXPECT_MEM_EQ(src3, dst3);

  {
    // This is not a writable file.
    auto result = f->write(initial_content, sizeof(initial_content));
    EXPECT_EQ(result.value, size_t(0));
    EXPECT_TRUE(f->error());
    EXPECT_TRUE(result.has_error());
  }

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, ReadSeekCurAndRead) {
  LocaleGuard locale;
  const char initial_content[] = "1234567890987654321";
  constexpr size_t FILE_BUFFER_SIZE = sizeof(initial_content);
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "r");
  f->reset_and_fill(initial_content, sizeof(initial_content));

  constexpr size_t READ_SIZE = 5;
  char data[READ_SIZE];
  data[READ_SIZE - 1] = '\0';
  ASSERT_EQ(f->read(data, READ_SIZE - 1).value, READ_SIZE - 1);
  ASSERT_STREQ(data, "1234");
  ASSERT_EQ(f->seek(5, SEEK_CUR).value(), 0);
  ASSERT_EQ(f->read(data, READ_SIZE - 1).value, READ_SIZE - 1);
  ASSERT_STREQ(data, "0987");
  ASSERT_EQ(f->seek(-5, SEEK_CUR).value(), 0);
  ASSERT_EQ(f->read(data, READ_SIZE - 1).value, READ_SIZE - 1);
  ASSERT_STREQ(data, "9098");
  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, AppendOnly) {
  LocaleGuard locale;
  const char initial_content[] = "1234567890987654321";
  const char write_data[] = "append";
  constexpr size_t FILE_BUFFER_SIZE = sizeof(write_data) * 3 / 2;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "a");
  f->reset_and_fill(initial_content, sizeof(initial_content));

  constexpr size_t READ_SIZE = 5;
  char read_data[READ_SIZE];

  {
    // This is not a readable file.
    auto result = f->read(read_data, READ_SIZE);
    EXPECT_EQ(result.value, size_t(0));
    EXPECT_TRUE(f->error());
    EXPECT_TRUE(result.has_error());
  }

  // Write should succeed but will be buffered in the file stream.
  ASSERT_EQ(f->write(write_data, sizeof(write_data)).value, sizeof(write_data));
  EXPECT_EQ(f->get_pos(), size_t(0));
  // Flushing will write to the file.
  EXPECT_EQ(f->flush(), int(0));
  EXPECT_EQ(f->get_pos(), sizeof(write_data) + sizeof(initial_content));

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, WriteUpdate) {
  LocaleGuard locale;
  const char data[] = "hello, file";
  constexpr size_t FILE_BUFFER_SIZE = sizeof(data) * 3 / 2;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "w+");

  ASSERT_EQ(sizeof(data), f->write(data, sizeof(data)).value);
  EXPECT_EQ(f->get_pos(), size_t(0)); // Data is buffered in the file stream

  ASSERT_EQ(f->seek(0, SEEK_SET).value(), 0);

  // Seek flushes the stream buffer so we can read the previously written data.
  char read_data[sizeof(data)];
  ASSERT_EQ(f->read(read_data, sizeof(data)).value, sizeof(data));
  EXPECT_STREQ(read_data, data);

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, ReadUpdate) {
  LocaleGuard locale;
  const char initial_content[] = "1234567890987654321";
  constexpr size_t FILE_BUFFER_SIZE = sizeof(initial_content);
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "r+");
  f->reset_and_fill(initial_content, sizeof(initial_content));

  constexpr size_t READ_SIZE = sizeof(initial_content) / 2;
  char read_data[READ_SIZE];
  ASSERT_EQ(READ_SIZE, f->read(read_data, READ_SIZE).value);
  EXPECT_FALSE(f->iseof());
  // Reading less than file buffer worth will still read one
  // full buffer worth of data.
  EXPECT_STREQ(file_buffer, initial_content);
  EXPECT_STREQ(file_buffer, f->get_str());
  EXPECT_EQ(FILE_BUFFER_SIZE, f->get_pos());
  // The read data should match what was supposed to be read anyway.
  MemoryView src1(initial_content, READ_SIZE), dst1(read_data, READ_SIZE);
  EXPECT_MEM_EQ(src1, dst1);

  ASSERT_EQ(f->seek(0, SEEK_SET).value(), 0);
  const char write_data[] = "hello, file";
  ASSERT_EQ(sizeof(write_data), f->write(write_data, sizeof(write_data)).value);
  EXPECT_STREQ(file_buffer, write_data);
  ASSERT_EQ(f->flush(), 0);
  MemoryView dst2(f->get_str(), sizeof(write_data)),
      src2(write_data, sizeof(write_data));
  EXPECT_MEM_EQ(src2, dst2);

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, AppendUpdate) {
  LocaleGuard locale;
  const char initial_content[] = "1234567890987654321";
  const char data[] = "hello, file";
  constexpr size_t FILE_BUFFER_SIZE = sizeof(data) * 3 / 2;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "a+");
  f->reset_and_fill(initial_content, sizeof(initial_content));

  ASSERT_EQ(sizeof(data), f->write(data, sizeof(data)).value);
  EXPECT_EQ(f->get_pos(), size_t(0)); // Data is buffered in the file stream
  ASSERT_EQ(f->flush(), 0);
  // The flush should write |data| to the endof the file.
  EXPECT_EQ(f->get_pos(), sizeof(data) + sizeof(initial_content));

  ASSERT_EQ(f->seek(0, SEEK_SET).value(), 0);
  // Seeking to the beginning of the file should not affect the place
  // where write happens.
  ASSERT_EQ(sizeof(data), f->write(data, sizeof(data)).value);
  ASSERT_EQ(f->flush(), 0);
  EXPECT_EQ(f->get_pos(), sizeof(data) * 2 + sizeof(initial_content));
  MemoryView src1(initial_content, sizeof(initial_content)),
      dst1(f->get_str(), sizeof(initial_content));
  EXPECT_MEM_EQ(src1, dst1);
  MemoryView src2(data, sizeof(data)),
      dst2(f->get_str() + sizeof(initial_content), sizeof(data));
  EXPECT_MEM_EQ(src2, dst2);
  MemoryView src3(data, sizeof(data)),
      dst3(f->get_str() + sizeof(initial_content) + sizeof(data), sizeof(data));
  EXPECT_MEM_EQ(src3, dst3);

  // Reads can happen from any point.
  ASSERT_EQ(f->seek(0, SEEK_SET).value(), 0);
  constexpr size_t READ_SIZE = 10;
  char read_data[READ_SIZE];
  ASSERT_EQ(READ_SIZE, f->read(read_data, READ_SIZE).value);
  MemoryView src4(initial_content, READ_SIZE), dst4(read_data, READ_SIZE);
  EXPECT_MEM_EQ(src4, dst4);

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, UngetcPreservesErrorIndicator) {
  const int modes[] = {_IOFBF, _IONBF};
  for (int mode : modes) {
    char buffer[8];
    StringFile *f = new_string_file(buffer, sizeof(buffer), mode, false, "r");
    char input;
    ASSERT_EQ(f->read(&input, 1).value, size_t(0));
    ASSERT_TRUE(f->iseof());
    f->set_error_unlocked();
    EXPECT_EQ(f->ungetc(EOF), EOF);
    EXPECT_TRUE(f->error());
    EXPECT_TRUE(f->iseof());

    ASSERT_EQ(f->ungetc('x'), int('x'));
    EXPECT_TRUE(f->error());
    EXPECT_FALSE(f->iseof());
    ASSERT_EQ(f->read(&input, 1).value, size_t(1));
    EXPECT_EQ(input, 'x');
    EXPECT_TRUE(f->error());
    f->clearerr();
    EXPECT_FALSE(f->error());
    EXPECT_EQ(f->close(), 0);
  }
}

TEST(LlvmLibcFileTest, PushbackAfterSuccessfulOutputFlush) {
  const int modes[] = {_IOFBF, _IONBF};
  for (int mode : modes) {
    char buffer[16];
    auto *file = new_string_file(buffer, sizeof(buffer), mode, false, "w+");
    ASSERT_EQ(file->write("a", 1).value, size_t(1));
    ASSERT_EQ(file->flush(), 0);
    ASSERT_EQ(file->ungetc('x'), int('x'));
    char c;
    EXPECT_EQ(file->read(&c, 1).value, size_t(1));
    EXPECT_EQ(c, 'x');
    EXPECT_EQ(file->tell().value(), off_t(1));
    EXPECT_EQ(file->get_str()[0], 'a');
    EXPECT_EQ(file->close(), 0);

    file = new_string_file(buffer, sizeof(buffer), mode, false, "w+");
    ASSERT_EQ(file->write(L"a", 1).value, size_t(1));
    ASSERT_EQ(file->flush(), 0);
    ASSERT_EQ(file->ungetwc(L'x').value(), wint_t(L'x'));
    wchar_t wc;
    EXPECT_EQ(file->read(&wc, 1).value, size_t(1));
    EXPECT_EQ(wc, L'x');
    EXPECT_EQ(file->tell().value(), off_t(1));
    EXPECT_EQ(file->get_str()[0], 'a');
    EXPECT_EQ(file->close(), 0);
  }
}

class LimitPositionFile : public File {
  uint8_t storage[16];
  off_t offset;
  static ErrorOr<off_t> seek_at_limit(File *file, off_t, int) {
    return static_cast<LimitPositionFile *>(file)->offset;
  }
  static FileIOResult supply(File *, void *ptr, size_t size) {
    auto *bytes = static_cast<unsigned char *>(ptr);
    for (size_t i = 0; i != size; ++i)
      bytes[i] = 'a';
    return size;
  }
  static FileIOResult sink(File *, const void *, size_t size) { return size; }
  static int retire(File *) { return 0; }

public:
  explicit LimitPositionFile(off_t where)
      : File(sink, supply, seek_at_limit, retire, storage, sizeof(storage),
             _IOFBF, false, mode_flags("r+")),
        offset(where) {}
};

TEST(LlvmLibcFileTest, BufferedPositionOverflow) {
  constexpr off_t MAX = LIBC_NAMESPACE::cpp::numeric_limits<off_t>::max();
  constexpr off_t MIN = LIBC_NAMESPACE::cpp::numeric_limits<off_t>::min();
  LimitPositionFile byte(MAX - 1);
  ASSERT_EQ(byte.write("abc", 3).value, size_t(3));
  EXPECT_EQ(byte.tell().error(), EOVERFLOW);
  EXPECT_EQ(byte.get_position().error(), EOVERFLOW);
  LimitPositionFile wide(MAX - 1);
  ASSERT_EQ(wide.write(L"abc", 3).value, size_t(3));
  EXPECT_EQ(wide.tell().error(), EOVERFLOW);
  EXPECT_EQ(wide.get_position().error(), EOVERFLOW);

  LimitPositionFile input(16);
  char c;
  ASSERT_EQ(input.read(&c, 1).value, size_t(1));
  EXPECT_EQ(input.tell().value(), off_t(1));
  EXPECT_EQ(input.File::seek(MIN, SEEK_CUR).error(), EOVERFLOW);
  // Failed arithmetic must not discard the unread buffer.
  EXPECT_EQ(input.tell().value(), off_t(1));
  EXPECT_EQ(byte.close(), 0);
  EXPECT_EQ(wide.close(), 0);
  EXPECT_EQ(input.close(), 0);
}

TEST(LlvmLibcFileTest, UngetcReturnsConvertedByte) {
  const int modes[] = {_IOFBF, _IONBF};
  const int values[] = {0x100 + 'x', -2, 0x100};
  for (int mode : modes) {
    char buffer[8];
    StringFile *f = new_string_file(buffer, sizeof(buffer), mode, false, "r");
    for (int value : values) {
      EXPECT_EQ(f->ungetc(value), int(static_cast<unsigned char>(value)));
      unsigned char byte;
      ASSERT_EQ(f->read(&byte, 1).value, size_t(1));
      EXPECT_EQ(byte, static_cast<unsigned char>(value));
    }
    EXPECT_EQ(f->ungetc(EOF), EOF);
    EXPECT_EQ(f->close(), 0);
  }
}

TEST(LlvmLibcFileTest, InputFlushesLineBufferedOutputAtRequiredBoundaries) {
  const int modes[] = {_IONBF, _IOLBF, _IOFBF};
  for (int mode : modes) {
    char input_buffer[8], output_buffer[8];
    auto *input =
        new_string_file(input_buffer, sizeof(input_buffer), mode, false, "r");
    auto *output = new_string_file(output_buffer, sizeof(output_buffer), _IOLBF,
                                   false, "w");
    input->reset_and_fill("abcdefgh", 8);
    File::add_file(output);
    ASSERT_EQ(output->write("one", 3).value, size_t(3));
    char byte;
    ASSERT_EQ(input->read(&byte, 1).value, size_t(1));
    EXPECT_EQ(byte, 'a');
    EXPECT_EQ(output->get_pos(), mode == _IOFBF ? size_t(0) : size_t(3));
    ASSERT_EQ(output->write("two", 3).value, size_t(3));
    ASSERT_EQ(input->read(&byte, 1).value, size_t(1));
    EXPECT_EQ(byte, 'b');
    // A line-buffered input satisfied from its buffer does not trigger I/O.
    EXPECT_EQ(output->get_pos(), mode == _IONBF   ? size_t(6)
                                 : mode == _IOLBF ? size_t(3)
                                                  : size_t(0));
    EXPECT_EQ(input->close(), 0);
    EXPECT_EQ(output->close(), 0);
  }
}

namespace {
class ChunkedReadFile : public File {
  static FileIOResult read_chunk(File *file, void *output, size_t length) {
    auto *self = static_cast<ChunkedReadFile *>(file);
    ++self->calls;
    if (self->fail_at && self->calls == self->fail_at)
      return {0, EINTR};
    size_t count = self->remaining < 2 ? self->remaining : 2;
    if (count > length)
      count = length;
    for (size_t i = 0; i < count; ++i)
      static_cast<char *>(output)[i] = self->next++;
    self->remaining -= count;
    return count;
  }

public:
  size_t remaining = 8;
  unsigned calls = 0, fail_at = 0;
  char next = 'a';
  ChunkedReadFile(uint8_t *buffer, size_t size, int mode)
      : File(nullptr, read_chunk, nullptr, nullptr, buffer, size, mode, false,
             mode_flags("r")) {}
};
} // namespace

TEST(LlvmLibcFileTest, PositiveShortReadsAreNotEOF) {
  const int modes[] = {_IOFBF, _IOLBF, _IONBF};
  for (int mode : modes) {
    uint8_t buffer[4];
    ChunkedReadFile file(buffer, sizeof(buffer), mode);
    char data[8];
    auto result = file.read(data, sizeof(data));
    EXPECT_FALSE(result.has_error());
    ASSERT_EQ(result.value, sizeof(data));
    EXPECT_MEM_EQ(MemoryView(data, sizeof(data)), MemoryView("abcdefgh", 8));
    EXPECT_FALSE(file.iseof());
    EXPECT_EQ(file.read(data, 1).value, size_t(0));
    EXPECT_TRUE(file.iseof());
    EXPECT_FALSE(file.error());
  }
}

TEST(LlvmLibcFileTest, ShortReadsPreservePrefixOnInterruption) {
  const int modes[] = {_IOFBF, _IOLBF, _IONBF};
  for (int mode : modes) {
    uint8_t buffer[4];
    ChunkedReadFile file(buffer, sizeof(buffer), mode);
    file.fail_at = 2;
    char data[8];
    auto result = file.read(data, sizeof(data));
    EXPECT_EQ(result.error, EINTR);
    ASSERT_EQ(result.value, size_t(2));
    EXPECT_MEM_EQ(MemoryView(data, 2), MemoryView("ab", 2));
    EXPECT_FALSE(file.iseof());
    EXPECT_TRUE(file.error());
    file.clearerr();
    result = file.read(data, 6);
    ASSERT_EQ(result.value, size_t(6));
    EXPECT_FALSE(result.has_error());
    EXPECT_MEM_EQ(MemoryView(data, 6), MemoryView("cdefgh", 6));
    EXPECT_FALSE(file.iseof());
  }
}

TEST(LlvmLibcFileTest, SmallBuffer) {
  LocaleGuard locale;
  const char WRITE_DATA[] = "small buffer";
  constexpr size_t WRITE_SIZE = sizeof(WRITE_DATA);
  constexpr size_t FILE_BUFFER_SIZE = sizeof(WRITE_DATA) / 2 - 1;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "w");

  ASSERT_EQ(WRITE_SIZE, f->write(WRITE_DATA, WRITE_SIZE).value);
  // Since data much larger than the buffer is being written, all of it should
  // be available in the file without a flush operation.
  EXPECT_EQ(f->get_pos(), sizeof(WRITE_DATA));
  ASSERT_STREQ(f->get_str(), WRITE_DATA);

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, ZeroLengthBuffer) {
  LocaleGuard locale;
  const char WRITE_DATA[] = "small buffer";
  constexpr size_t WRITE_SIZE = sizeof(WRITE_DATA);
  StringFile *f_fbf = new_string_file(nullptr, 0, _IOFBF, true, "w");
  StringFile *f_lbf = new_string_file(nullptr, 0, _IOLBF, true, "w");
  StringFile *f_nbf = new_string_file(nullptr, 0, _IONBF, true, "w");

  ASSERT_EQ(WRITE_SIZE, f_fbf->write(WRITE_DATA, WRITE_SIZE).value);
  ASSERT_EQ(WRITE_SIZE, f_lbf->write(WRITE_DATA, WRITE_SIZE).value);
  ASSERT_EQ(WRITE_SIZE, f_nbf->write(WRITE_DATA, WRITE_SIZE).value);
  // Since there is no buffer space, all of the written data should
  // be available in the file without a flush operation.
  EXPECT_EQ(f_fbf->get_pos(), sizeof(WRITE_DATA));
  EXPECT_EQ(f_lbf->get_pos(), sizeof(WRITE_DATA));
  EXPECT_EQ(f_nbf->get_pos(), sizeof(WRITE_DATA));
  ASSERT_STREQ(f_fbf->get_str(), WRITE_DATA);
  ASSERT_STREQ(f_lbf->get_str(), WRITE_DATA);
  ASSERT_STREQ(f_nbf->get_str(), WRITE_DATA);

  ASSERT_EQ(f_fbf->close(), 0);
  ASSERT_EQ(f_lbf->close(), 0);
  ASSERT_EQ(f_nbf->close(), 0);
}

TEST(LlvmLibcFileTest, WriteNothing) {
  LocaleGuard locale;
  const char WRITE_DATA[] = "";
  constexpr size_t WRITE_SIZE = 0;
  constexpr size_t FILE_BUFFER_SIZE = 5;
  char file_buffer_fbf[FILE_BUFFER_SIZE];
  char file_buffer_lbf[FILE_BUFFER_SIZE];
  char file_buffer_nbf[FILE_BUFFER_SIZE];
  StringFile *f_fbf =
      new_string_file(file_buffer_fbf, FILE_BUFFER_SIZE, _IOFBF, false, "w");
  StringFile *f_lbf =
      new_string_file(file_buffer_lbf, FILE_BUFFER_SIZE, _IOLBF, false, "w");
  StringFile *f_nbf =
      new_string_file(file_buffer_nbf, FILE_BUFFER_SIZE, _IONBF, false, "w");

  ASSERT_EQ(WRITE_SIZE, f_fbf->write(WRITE_DATA, WRITE_SIZE).value);
  ASSERT_EQ(WRITE_SIZE, f_lbf->write(WRITE_DATA, WRITE_SIZE).value);
  ASSERT_EQ(WRITE_SIZE, f_nbf->write(WRITE_DATA, WRITE_SIZE).value);

  ASSERT_FALSE(f_fbf->error_unlocked());
  ASSERT_FALSE(f_lbf->error_unlocked());
  ASSERT_FALSE(f_nbf->error_unlocked());

  ASSERT_EQ(f_fbf->close(), 0);
  ASSERT_EQ(f_lbf->close(), 0);
  ASSERT_EQ(f_nbf->close(), 0);
}

TEST(LlvmLibcFileTest, WriteSplit) {
  LocaleGuard locale;
  constexpr size_t FILE_BUFFER_SIZE = 8;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "w");

  static constexpr size_t AVAIL = 12;
  f->seek(-AVAIL, SEEK_END);

  const char data[] = "hello";
  ASSERT_EQ(sizeof(data) - 1, f->write(data, sizeof(data) - 1).value);

  const char data2[] = " extra data";
  static constexpr size_t WR_EXPECTED = AVAIL - (sizeof(data) - 1);
  ASSERT_EQ(WR_EXPECTED, f->write(data2, sizeof(data2) - 1).value);
  EXPECT_TRUE(f->error());
  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, WideCharOrientation) {
  LocaleGuard locale;
  constexpr size_t FILE_BUFFER_SIZE = 512;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "w+");

  f->write(L"A", 1);

  auto write_res = f->write("B", 1);
  EXPECT_EQ(write_res.value, size_t(0));
  EXPECT_TRUE(f->error());

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, ByteCharOrientation) {
  LocaleGuard locale;
  constexpr size_t FILE_BUFFER_SIZE = 512;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "w+");

  f->write("A", 1);

  auto write_res = f->write(L"B", 1);
  EXPECT_EQ(write_res.value, size_t(0));
  EXPECT_TRUE(f->error());

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, PushbackBeforeBufferedLookahead) {
  const int modes[] = {_IOFBF, _IOLBF, _IONBF};
  for (int mode : modes) {
    char buffer[4];
    StringFile *f = new_string_file(buffer, sizeof(buffer), mode, false, "r");
    f->reset_and_fill("hello", 5);
    char data[3] = {};
    ASSERT_EQ(f->read(data, 1).value, size_t(1));
    ASSERT_EQ(data[0], 'h');
    // A scanner returns its nonmatching lookahead, then the caller pushes
    // another byte without consuming that lookahead.
    ASSERT_EQ(f->ungetc('h'), int('h'));
    ASSERT_EQ(f->ungetc('x'), int('x'));
    ASSERT_EQ(f->read(data, 0).value, size_t(0));
    ASSERT_EQ(f->read(data, 3).value, size_t(3));
    EXPECT_EQ(data[0], 'x');
    EXPECT_EQ(data[1], 'h');
    EXPECT_EQ(data[2], 'e');
    EXPECT_EQ(f->tell().value(), off_t(2));
    ASSERT_EQ(f->close(), 0);
  }
}

TEST(LlvmLibcFileTest, PushbackBoundaryPositionAndSeek) {
  char buffer[2];
  StringFile *f = new_string_file(buffer, sizeof(buffer), _IOFBF, false, "r");
  f->reset_and_fill("abcd", 4);
  char data[2];
  ASSERT_EQ(f->read(data, 2).value, size_t(2));
  ASSERT_EQ(f->read(data, 1).value, size_t(1));
  ASSERT_EQ(f->ungetc('c'), int('c'));
  ASSERT_EQ(f->ungetc('x'), int('x'));
  EXPECT_EQ(f->tell().value(), off_t(1));
  ASSERT_EQ(f->seek(0, SEEK_CUR).value(), 0);
  ASSERT_EQ(f->read(data, 1).value, size_t(1));
  EXPECT_EQ(data[0], 'b');
  ASSERT_EQ(f->read(data, 1).value, size_t(1));
  ASSERT_EQ(f->ungetc('c'), int('c'));
  ASSERT_EQ(f->ungetc('b'), int('b'));
  ASSERT_EQ(f->ungetc('x'), int('x'));
  EXPECT_EQ(f->tell().value(), off_t(0));
  ASSERT_EQ(f->flush(), 0);
  EXPECT_EQ(f->tell().value(), off_t(0));
  ASSERT_EQ(f->read(data, 1).value, size_t(1));
  EXPECT_EQ(data[0], 'a');
  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, Ungetwc) {
  LocaleGuard locale;
  constexpr size_t FILE_BUFFER_SIZE = 512;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "w+");

  f->write(L"A", 1);
  f->flush();
  f->seek(0, SEEK_SET);

  wchar_t ws_out[2];
  auto read_res = f->read(ws_out, 1);
  ASSERT_EQ(read_res.value, size_t(1));
  EXPECT_EQ(static_cast<uint32_t>(ws_out[0]), static_cast<uint32_t>(L'A'));

  auto unget_res = f->ungetwc(L'B');
  ASSERT_TRUE(unget_res.has_value());
  EXPECT_EQ(static_cast<uint32_t>(unget_res.value()),
            static_cast<uint32_t>(L'B'));
  auto read_res2 = f->read(ws_out, 1);

  ASSERT_EQ(read_res2.value, size_t(1));
  EXPECT_EQ(static_cast<uint32_t>(ws_out[0]), static_cast<uint32_t>(L'B'));

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, WideStringIO) {
  LocaleGuard locale;
  constexpr size_t FILE_BUFFER_SIZE = 100;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "w+");
  ASSERT_FALSE(f == nullptr);

  const wchar_t *ws = L"Hello, World!";
  size_t len = 13;

  auto write_res = f->write(ws, len);
  ASSERT_FALSE(write_res.has_error());
  EXPECT_EQ(write_res.value, len);

  ASSERT_EQ(f->flush(), 0); // Ensure everything is written to StringFile

  ASSERT_EQ(f->seek(0, SEEK_SET).value(), 0);

  wchar_t read_buf[20];
  auto read_res = f->read(read_buf, len);
  ASSERT_FALSE(read_res.has_error());
  EXPECT_EQ(read_res.value, len);

  for (size_t i = 0; i < len; ++i) {
    EXPECT_EQ(static_cast<uint32_t>(read_buf[i]), static_cast<uint32_t>(ws[i]));
  }

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, TrySetOrientation) {
  LocaleGuard locale;
  constexpr size_t FILE_BUFFER_SIZE = 100;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "r+");
  ASSERT_FALSE(f == nullptr);

  EXPECT_EQ(static_cast<uint32_t>(f->get_orientation()),
            static_cast<uint32_t>(File::Orientation::UNORIENTED));

  EXPECT_EQ(
      static_cast<uint32_t>(f->try_set_orientation(File::Orientation::WIDE)),
      static_cast<uint32_t>(File::Orientation::WIDE));
  EXPECT_EQ(static_cast<uint32_t>(f->get_orientation()),
            static_cast<uint32_t>(File::Orientation::WIDE));

  EXPECT_EQ(
      static_cast<uint32_t>(f->try_set_orientation(File::Orientation::BYTE)),
      static_cast<uint32_t>(File::Orientation::WIDE)); // Cannot change
  EXPECT_EQ(static_cast<uint32_t>(f->get_orientation()),
            static_cast<uint32_t>(File::Orientation::WIDE));

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, UngetwcMultiByte) {
  LocaleGuard locale;
  constexpr size_t FILE_BUFFER_SIZE = 512;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "w+");

  f->write(L"€", 1);
  f->flush();
  f->seek(0, SEEK_SET);

  wchar_t ws_out[2];
  auto read_res = f->read(ws_out, 1);
  ASSERT_EQ(read_res.value, size_t(1));
  EXPECT_EQ(static_cast<uint32_t>(ws_out[0]), static_cast<uint32_t>(L'€'));

  auto unget_res = f->ungetwc(L'¢');
  ASSERT_TRUE(unget_res.has_value());
  EXPECT_EQ(static_cast<uint32_t>(unget_res.value()),
            static_cast<uint32_t>(L'¢'));

  auto read_res2 = f->read(ws_out, 1);
  ASSERT_EQ(read_res2.value, size_t(1));
  EXPECT_EQ(static_cast<uint32_t>(ws_out[0]), static_cast<uint32_t>(L'¢'));

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, UngetwcUnbufferedMultiByte) {
  LocaleGuard locale;
  StringFile *f = new_string_file(nullptr, 0, _IONBF, true, "w+");
  ASSERT_FALSE(f == nullptr);

  f->write(L"€", 1);
  f->seek(0, SEEK_SET);

  wchar_t ws_out[2];
  auto read_res = f->read(ws_out, 1);
  ASSERT_EQ(read_res.value, size_t(1));
  EXPECT_EQ(static_cast<uint32_t>(ws_out[0]), static_cast<uint32_t>(L'€'));

  auto unget_res = f->ungetwc(L'¢');
  ASSERT_TRUE(unget_res.has_value());
  EXPECT_EQ(static_cast<uint32_t>(unget_res.value()),
            static_cast<uint32_t>(L'¢'));

  auto read_res2 = f->read(ws_out, 1);
  ASSERT_EQ(read_res2.value, size_t(1));
  EXPECT_EQ(static_cast<uint32_t>(ws_out[0]), static_cast<uint32_t>(L'¢'));

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, WideStringIO_Multibyte) {
  LocaleGuard locale;
  constexpr size_t FILE_BUFFER_SIZE = 100;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "w+");
  ASSERT_FALSE(f == nullptr);

  const wchar_t *ws = L"Hello € World!";
  size_t len = 14;

  auto write_res = f->write(ws, len);
  ASSERT_FALSE(write_res.has_error());
  EXPECT_EQ(write_res.value, len);

  ASSERT_EQ(f->flush(), 0);

  ASSERT_EQ(f->seek(0, SEEK_SET).value(), 0);

  wchar_t read_buf[20];
  auto read_res = f->read(read_buf, len);
  ASSERT_FALSE(read_res.has_error());
  EXPECT_EQ(read_res.value, len);

  for (size_t i = 0; i < len; ++i) {
    EXPECT_EQ(static_cast<uint32_t>(read_buf[i]), static_cast<uint32_t>(ws[i]));
  }

  ASSERT_EQ(f->close(), 0);
}

#ifdef LIBC_FULL_BUILD
TEST(LlvmLibcFileTest, WideOrientationRetainsEncoding) {
  LocaleGuard utf8;
  char buffer[32];
  StringFile *f = new_string_file(buffer, sizeof(buffer), _IOFBF, false, "w+");
  ASSERT_NE(f, nullptr);
  f->try_set_orientation(File::Orientation::WIDE);
  {
    LocaleGuard c(false);
    const wchar_t text[] = {0xe9};
    auto written = f->write(text, 1);
    EXPECT_FALSE(written.has_error());
    EXPECT_EQ(written.value, size_t(1));
    ASSERT_EQ(f->flush(), 0);
    ASSERT_EQ(f->seek(0, SEEK_SET).value(), 0);
    wchar_t wc;
    auto read = f->read(&wc, 1);
    EXPECT_FALSE(read.has_error());
    EXPECT_EQ(read.value, size_t(1));
    EXPECT_EQ(wc, wchar_t(0xe9));
  }
  EXPECT_EQ(f->close(), 0);

  f = new_string_file(buffer, sizeof(buffer), _IOFBF, false, "w+");
  ASSERT_NE(f, nullptr);
  {
    LocaleGuard c(false);
    f->try_set_orientation(File::Orientation::WIDE);
  }
  const wchar_t text[] = {0x100};
  auto written = f->write(text, 1);
  EXPECT_TRUE(written.has_error());
  EXPECT_EQ(written.error, EILSEQ);
  EXPECT_EQ(f->close(), 0);
}
#endif

TEST(LlvmLibcFileTest, SeekResetsMbstate) {
  LocaleGuard locale;
  constexpr size_t FILE_BUFFER_SIZE = 100;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "r+");
  ASSERT_FALSE(f == nullptr);

  f->reset_and_fill("\xE2\x82", 2);

  wchar_t ws_out[1];
  auto read_res = f->read(ws_out, 1);
  EXPECT_EQ(read_res.value, size_t(0));
  EXPECT_TRUE(f->error());

  f->reset_and_fill("A", 1);
  f->seek(0, SEEK_SET);
  f->clearerr();

  auto read_res2 = f->read(ws_out, 1);
  EXPECT_EQ(read_res2.value, size_t(1));
  EXPECT_EQ(static_cast<uint32_t>(ws_out[0]), static_cast<uint32_t>(L'A'));

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, ReadWideNotStopAtNewline) {
  LocaleGuard locale;
  constexpr size_t FILE_BUFFER_SIZE = 100;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "w+");
  ASSERT_FALSE(f == nullptr);

  const wchar_t *ws = L"Hello\nWorld!";
  size_t len = 12;

  auto write_res = f->write(ws, len);
  ASSERT_FALSE(write_res.has_error());
  EXPECT_EQ(write_res.value, len);

  ASSERT_EQ(f->flush(), 0);
  ASSERT_EQ(f->seek(0, SEEK_SET).value(), 0);

  wchar_t read_buf[20];
  auto read_res = f->read(read_buf, len);
  ASSERT_FALSE(read_res.has_error());
  // Should NOT stop at newline, so should read all 12 characters.
  EXPECT_EQ(read_res.value, len);
  EXPECT_EQ(static_cast<uint32_t>(read_buf[5]), static_cast<uint32_t>(L'\n'));
  EXPECT_EQ(static_cast<uint32_t>(read_buf[11]), static_cast<uint32_t>(L'!'));

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, UngetwcWEOF) {
  LocaleGuard locale;
  constexpr size_t FILE_BUFFER_SIZE = 100;
  char file_buffer[FILE_BUFFER_SIZE];
  StringFile *f =
      new_string_file(file_buffer, FILE_BUFFER_SIZE, _IOFBF, false, "r+");
  ASSERT_FALSE(f == nullptr);

  EXPECT_EQ(static_cast<uint32_t>(f->get_orientation()),
            static_cast<uint32_t>(File::Orientation::UNORIENTED));

  auto unget_res = f->ungetwc(WEOF);
  ASSERT_TRUE(unget_res.has_value());
  EXPECT_EQ(static_cast<uint32_t>(unget_res.value()),
            static_cast<uint32_t>(WEOF));

  EXPECT_EQ(static_cast<uint32_t>(f->get_orientation()),
            static_cast<uint32_t>(File::Orientation::UNORIENTED));

  ASSERT_EQ(f->close(), 0);
}

// A File subclass with a platform_write that simulates short writes.
// This models the behavior of write(2) on pipes, sockets, or FIFOs where
// the kernel may write fewer bytes than requested.
class ShortWriteFile : public File {
  static constexpr size_t SIZE = 512;
  size_t pos;
  char str[SIZE] = {0};
  size_t max_write;
  size_t fail_after = SIZE;
  int failure = EIO;
  unsigned calls = 0;

  static FileIOResult short_write(LIBC_NAMESPACE::File *f, const void *data,
                                  size_t len) {
    ShortWriteFile *sf = static_cast<ShortWriteFile *>(f);
    ++sf->calls;
    if (sf->pos >= sf->fail_after)
      return {0, sf->failure};
    // Simulate a short write: write at most max_write bytes per call.
    size_t to_write = len < sf->max_write ? len : sf->max_write;
    if (to_write > sf->fail_after - sf->pos)
      to_write = sf->fail_after - sf->pos;
    for (size_t i = 0; i < to_write && sf->pos < SIZE; ++i, ++sf->pos)
      sf->str[sf->pos] = reinterpret_cast<const char *>(data)[i];
    return to_write;
  }

  static FileIOResult short_read(LIBC_NAMESPACE::File *f, void *data,
                                 size_t len) {
    ShortWriteFile *sf = static_cast<ShortWriteFile *>(f);
    size_t i = 0;
    for (i = 0; i < len && sf->pos < SIZE; ++i)
      reinterpret_cast<char *>(data)[i] = sf->str[sf->pos + i];
    sf->pos += i;
    return i;
  }

  static ErrorOr<off_t> short_seek(LIBC_NAMESPACE::File *f, off_t offset,
                                   int whence) {
    ShortWriteFile *sf = static_cast<ShortWriteFile *>(f);
    if (whence == SEEK_SET)
      sf->pos = offset;
    if (whence == SEEK_CUR)
      sf->pos += offset;
    if (whence == SEEK_END)
      sf->pos = SIZE + offset;
    return sf->pos;
  }

  static int short_close(LIBC_NAMESPACE::File *f) {
    delete reinterpret_cast<ShortWriteFile *>(f);
    return 0;
  }

public:
  explicit ShortWriteFile(char *buffer, size_t buflen, int bufmode, bool owned,
                          ModeFlags modeflags, size_t max_write_bytes)
      : LIBC_NAMESPACE::File(&short_write, &short_read, &short_seek,
                             &short_close, reinterpret_cast<uint8_t *>(buffer),
                             buflen, bufmode, owned, modeflags),
        pos(0), max_write(max_write_bytes) {}

  void reset() { pos = 0; }
  size_t get_pos() const { return pos; }
  char *get_str() { return str; }
  void set_max_write(size_t maximum) { max_write = maximum; }
  void fail_at(size_t offset, int error = EIO) {
    fail_after = offset;
    failure = error;
  }
  unsigned write_calls() const { return calls; }
};

// A real error after a partial UTF-8 character preserves its committed prefix.
TEST(LlvmLibcFileTest, PartialWideCharWriteDetected) {
  LocaleGuard locale;
  LIBC_NAMESPACE::AllocChecker ac;
  // Unbuffered so writes go directly to platform_write, limited to 2 bytes.
  ShortWriteFile *f = new (ac) ShortWriteFile(
      nullptr, 0, _IONBF, true, LIBC_NAMESPACE::File::mode_flags("w"),
      /*max_write_bytes=*/2);
  ASSERT_FALSE(f == nullptr);
  f->fail_at(2, EAGAIN);

  // € (U+20AC) encodes to 3 UTF-8 bytes: 0xE2 0x82 0xAC.
  // With max_write=2, only 2 of the 3 bytes will be accepted.
  const wchar_t euro = L'€';
  auto result = f->write(&euro, 1);

  // The incomplete character must not be counted as written.
  EXPECT_TRUE(result.has_error());
  EXPECT_EQ(result.error, EAGAIN);
  EXPECT_EQ(result.value, size_t(0));

  // The error indicator on the stream should be set.
  EXPECT_TRUE(f->error());

  ASSERT_EQ(f->close(), 0);
}

TEST(LlvmLibcFileTest, ShortFlushDoesNotReplayCommittedPrefix) {
  LocaleGuard locale;
  char buffer[8];
  ShortWriteFile file(buffer, sizeof(buffer), _IOFBF, false,
                      File::mode_flags("w"), 2);
  file.fail_at(2);
  ASSERT_EQ(file.write("abcdef", 6).value, size_t(6));
  EXPECT_EQ(file.flush(), EIO);
  EXPECT_EQ(file.get_pos(), size_t(2));
  file.fail_at(4);
  EXPECT_EQ(file.flush(), EIO);
  EXPECT_EQ(file.get_pos(), size_t(4));
  file.fail_at(512);
  EXPECT_EQ(file.flush(), 0);
  EXPECT_EQ(file.get_pos(), size_t(6));
  EXPECT_MEM_EQ(MemoryView("abcdef", 6), MemoryView(file.get_str(), 6));
}

TEST(LlvmLibcFileTest, ShortSeekPreservesOnlyUnconsumedData) {
  LocaleGuard locale;
  char buffer[8];
  ShortWriteFile file(buffer, sizeof(buffer), _IOFBF, false,
                      File::mode_flags("w"), 2);
  file.fail_at(2);
  ASSERT_EQ(file.write("abcdef", 6).value, size_t(6));
  auto result = file.seek(0, SEEK_SET);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), EIO);
  EXPECT_EQ(file.get_pos(), size_t(2));
  file.set_max_write(8);
  file.fail_at(512);
  ASSERT_TRUE(file.seek(0, SEEK_SET).has_value());
  EXPECT_EQ(file.get_pos(), size_t(0));
  EXPECT_MEM_EQ(MemoryView("abcdef", 6), MemoryView(file.get_str(), 6));
}

TEST(LlvmLibcFileTest, ShortCloseConsumesStream) {
  LocaleGuard locale;
  LIBC_NAMESPACE::AllocChecker ac;
  char buffer[8];
  auto *file = new (ac) ShortWriteFile(buffer, sizeof(buffer), _IOFBF, false,
                                      File::mode_flags("w"), 2);
  ASSERT_TRUE(bool(ac));
  file->fail_at(2);
  File *previous = File::get_first_file();
  File::add_file(file);
  ASSERT_EQ(file->write("abcdef", 6).value, size_t(6));
  EXPECT_EQ(file->close(), EIO);
  EXPECT_EQ(File::get_first_file(), previous);
}

TEST(LlvmLibcFileTest, ShortBufferedWriteRetainsOldDataWithoutAcceptingNewData) {
  LocaleGuard locale;
  char buffer[8];
  ShortWriteFile file(buffer, sizeof(buffer), _IOFBF, false,
                      File::mode_flags("w"), 4);
  file.fail_at(4);
  ASSERT_EQ(file.write("abcdef", 6).value, size_t(6));
  auto result = file.write("ghijkl", 6);
  EXPECT_EQ(result.value, size_t(0));
  EXPECT_EQ(result.error, EIO);
  file.set_max_write(32);
  file.fail_at(512);
  ASSERT_EQ(file.write("ghijkl", 6).value, size_t(6));
  ASSERT_EQ(file.flush(), 0);
  EXPECT_EQ(file.get_pos(), size_t(12));
  EXPECT_MEM_EQ(MemoryView("abcdefghijkl", 12), MemoryView(file.get_str(), 12));
}

TEST(LlvmLibcFileTest, ShortPreflushRetainsOldDataWithoutWritingNewData) {
  LocaleGuard locale;
  char buffer[8];
  ShortWriteFile file(buffer, sizeof(buffer), _IOFBF, false,
                      File::mode_flags("w"), 4);
  file.fail_at(4);
  ASSERT_EQ(file.write("abcdef", 6).value, size_t(6));
  const char data[] = "ghijklmnopqrstuvwxyz";
  auto result = file.write(data, 20);
  EXPECT_EQ(result.value, size_t(0));
  EXPECT_EQ(result.error, EIO);
  file.set_max_write(32);
  file.fail_at(512);
  ASSERT_EQ(file.write(data, 20).value, size_t(20));
  EXPECT_EQ(file.get_pos(), size_t(26));
  EXPECT_MEM_EQ(MemoryView("abcdefghijklmnopqrstuvwxyz", 26),
                MemoryView(file.get_str(), 26));
}

TEST(LlvmLibcFileTest, ZeroProgressFlushDoesNotReportSuccess) {
  LocaleGuard locale;
  char buffer[8];
  ShortWriteFile file(buffer, sizeof(buffer), _IOFBF, false,
                      File::mode_flags("w"), 0);
  ASSERT_EQ(file.write("data", 4).value, size_t(4));
  EXPECT_EQ(file.flush(), EIO);
  EXPECT_EQ(file.get_pos(), size_t(0));
  file.set_max_write(8);
  EXPECT_EQ(file.flush(), 0);
  EXPECT_EQ(file.get_pos(), size_t(4));
  EXPECT_MEM_EQ(MemoryView("data", 4), MemoryView(file.get_str(), 4));
}

TEST(LlvmLibcFileTest, PositiveShortWritesCompleteAcrossBufferModes) {
  LocaleGuard locale;
  const int modes[] = {_IONBF, _IOFBF, _IOLBF};
  const size_t lengths[] = {6, 16, 20};
  const char data[] = "abc\nefghijklmnopqrst";
  for (int mode : modes) {
    for (size_t len : lengths) {
      char buffer[8];
      ShortWriteFile file(buffer, sizeof(buffer), mode, false,
                          File::mode_flags("w"), 2);
      auto result = file.write(data, len);
      EXPECT_EQ(result.value, len);
      EXPECT_EQ(result.error, 0);
      ASSERT_EQ(file.flush(), 0);
      EXPECT_FALSE(file.error());
      EXPECT_EQ(file.get_pos(), len);
      EXPECT_MEM_EQ(MemoryView(data, len), MemoryView(file.get_str(), len));
    }
  }
}

TEST(LlvmLibcFileTest, ShortWritesStopOnZeroOrErrorWithoutRetry) {
  LocaleGuard locale;
  const int modes[] = {_IONBF, _IOFBF, _IOLBF};
  const int errors[] = {0, EINTR, EAGAIN};
  const size_t stops[] = {2, 10};
  const char data[] = "abcdefghijklmno\n";
  for (int mode : modes) {
    for (int error : errors) {
      for (size_t stop : stops) {
        char buffer[8];
        ShortWriteFile file(buffer, sizeof(buffer), mode, false,
                            File::mode_flags("w"), 2);
        file.fail_at(stop, error);
        auto result = file.write(data, 16);
        EXPECT_EQ(result.value, stop);
        EXPECT_EQ(result.error, error ? error : EIO);
        EXPECT_TRUE(file.error());
        EXPECT_EQ(file.write_calls(), unsigned(stop / 2 + 1));
        file.fail_at(512);
        file.clearerr();
        result = file.write(data + stop, 16 - stop);
        EXPECT_EQ(result.value, size_t(16 - stop));
        EXPECT_EQ(result.error, 0);
        EXPECT_EQ(file.flush(), 0);
        EXPECT_EQ(file.get_pos(), size_t(16));
        EXPECT_MEM_EQ(MemoryView(data, 16), MemoryView(file.get_str(), 16));
      }
    }
  }
}

TEST(LlvmLibcFileTest, WideFlushRetainsOnlyUnwrittenEncodingBytes) {
  LocaleGuard locale;
  char buffer[16];
  ShortWriteFile file(buffer, sizeof(buffer), _IOFBF, false,
                      File::mode_flags("w"), 1);
  ASSERT_EQ(file.write(L"\u20acX", 2).value, size_t(2));
  file.fail_at(2, EINTR);
  EXPECT_EQ(file.flush(), EINTR);
  EXPECT_EQ(file.write_calls(), 3U);
  EXPECT_EQ(file.get_pos(), size_t(2));
  file.fail_at(512);
  file.clearerr();
  EXPECT_EQ(file.flush(), 0);
  EXPECT_EQ(file.get_pos(), size_t(4));
  EXPECT_MEM_EQ(MemoryView("\xe2\x82\xacX", 4),
                MemoryView(file.get_str(), 4));
}

TEST(LlvmLibcFileTest, FileLockRAII) {
  LocaleGuard locale;
  StringFile *f = new_string_file(nullptr, 0, _IONBF, false, "w+");
  ASSERT_FALSE(f == nullptr);

  {
    File::FileLock lock(f);
    ASSERT_EQ(f->write_unlocked("abc", 3).value, size_t(3));
  }

  // After FileLock is destroyed, operations under lock still acquire and
  // release cleanly.
  ASSERT_EQ(f->write("def", 3).value, size_t(3));
  ASSERT_EQ(f->close(), 0);
}

class ClosingInputFile : public File {
  uint8_t storage[8];
  static FileIOResult read_bytes(File *file, void *data, size_t len) {
    auto *self = static_cast<ClosingInputFile *>(file);
    size_t count = 6 - self->offset;
    if (count > len)
      count = len;
    for (size_t i = 0; i < count; ++i)
      static_cast<char *>(data)[i] = "abcdef"[self->offset + i];
    self->offset += count;
    return count;
  }
  static ErrorOr<off_t> seek_bytes(File *file, off_t offset, int whence) {
    auto *self = static_cast<ClosingInputFile *>(file);
    ++self->seeks;
    if (self->seek_error)
      return LIBC_NAMESPACE::Error(self->seek_error);
    if (whence != SEEK_CUR)
      return LIBC_NAMESPACE::Error(EINVAL);
    self->offset += offset;
    return self->offset;
  }
  static int close_file(File *file) {
    auto *self = static_cast<ClosingInputFile *>(file);
    ++self->closes;
    return self->close_error;
  }

public:
  off_t offset = 0;
  int seek_error = 0, close_error = 0;
  unsigned seeks = 0, closes = 0;
  ClosingInputFile()
      : File(nullptr, read_bytes, seek_bytes, close_file, storage,
             sizeof(storage), _IOFBF, false, mode_flags("r")) {}
};

TEST(LlvmLibcFileTest, CloseInputConsumesResourcesAfterSynchronizationFailure) {
  const int errors[] = {0, EIO, EINTR, ESPIPE};
  for (int error : errors) {
    ClosingInputFile file;
    file.seek_error = error;
    file.close_error = EBADF;
    char c;
    ASSERT_EQ(file.read(&c, 1).value, size_t(1));
    EXPECT_EQ(file.close(), error && error != ESPIPE ? error : EBADF);
    EXPECT_EQ(file.seeks, 1U);
    EXPECT_EQ(file.closes, 1U);
    EXPECT_EQ(file.offset, off_t(error ? 6 : 1));
  }
}

TEST(LlvmLibcFileTest, CloseDiscardAndEofDoNotSeek) {
  const bool states[] = {false, true};
  for (bool at_eof : states) {
    ClosingInputFile file;
    char bytes[8];
    auto result = file.read(bytes, at_eof ? sizeof(bytes) : 1);
    ASSERT_EQ(result.value, size_t(at_eof ? 6 : 1));
    ASSERT_EQ(file.iseof(), at_eof);
    EXPECT_EQ(file.close(at_eof), 0);
    EXPECT_EQ(file.seeks, 0U);
    EXPECT_EQ(file.closes, 1U);
  }
}

namespace {
class NativeWideFile : public File {
  static FileIOResult byte_write(File *, const void *, size_t) {
    return {0, EBADF};
  }
  static FileIOResult byte_read(File *, void *, size_t) { return {0, EBADF}; }
  static int close_file(File *) { return 0; }
  static ErrorOr<off_t> seek_file(File *file, off_t offset, int whence) {
    auto *f = static_cast<NativeWideFile *>(file);
    if (whence != SEEK_CUR || offset != 0)
      return LIBC_NAMESPACE::Error(EINVAL);
    return static_cast<off_t>(f->position);
  }
  static FileIOResult wide_write(File *file, const wchar_t *data, size_t size) {
    auto *f = static_cast<NativeWideFile *>(file);
    size_t count = size < f->limit ? size : f->limit;
    for (size_t i = 0; i < count; ++i)
      f->last = data[i];
    f->position += count;
    return {count, f->injected_error};
  }

public:
  size_t position = 0;
  size_t limit = 8;
  wchar_t last = 0;
  int injected_error = 0;

  NativeWideFile()
      : File(byte_write, byte_read, seek_file, close_file, nullptr, 0, _IONBF,
             false, ModeFlags(OpenMode::WRITE), nullptr, false, wide_write) {}
};
} // namespace

TEST(LlvmLibcFileTest, NativeWideSinkUsesCharacterOffsetsWithoutEncoding) {
  LocaleGuard locale;
  NativeWideFile file;
  const wchar_t text[] = {L'a', 0x20ac, 0x1f642};
  auto result = file.write(text, 3);
  ASSERT_EQ(result.error, 0);
  EXPECT_EQ(result.value, size_t(3));
  EXPECT_EQ(file.last, wchar_t(0x1f642));
  EXPECT_EQ(file.tell().value(), off_t(3));
  EXPECT_EQ(file.get_orientation(), File::Orientation::WIDE);
  EXPECT_EQ(file.flush(), 0);
  EXPECT_EQ(file.tell().value(), off_t(3));
}

TEST(LlvmLibcFileTest, NativeWideSinkRetainsPartialProgressAndError) {
  LocaleGuard locale;
  NativeWideFile file;
  file.limit = 1;
  auto result = file.write(L"ab", 2);
  EXPECT_EQ(result.value, size_t(1));
  EXPECT_EQ(result.error, EIO);
  EXPECT_TRUE(file.error());
  EXPECT_EQ(file.tell().value(), off_t(1));
  file.injected_error = ENOSPC;
  result = file.write(L"cd", 2);
  EXPECT_EQ(result.value, size_t(1));
  EXPECT_EQ(result.error, ENOSPC);
  EXPECT_EQ(file.tell().value(), off_t(2));
}
