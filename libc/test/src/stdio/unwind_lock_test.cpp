//===-- Stream lock cleanup during provider unwind ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/stdio_macros.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/File/file.h"
#include "src/stdio/fflush.h"
#include "src/stdio/vfprintf.h"
#include "src/stdio/vfscanf.h"
#include "src/stdio_ext/_flushlbf.h"
#include "test/UnitTest/Test.h"
#include "test/src/locale/LocaleGuard.h"
#include <pthread.h>
#include <stdarg.h>

using LIBC_NAMESPACE::File;
using LIBC_NAMESPACE::FileIOResult;

// Isolate list traversal from unrelated host standard-stream implementations.
namespace LIBC_NAMESPACE_DECL {
FILE *stdin = nullptr;
FILE *stdout = nullptr;
FILE *stderr = nullptr;
} // namespace LIBC_NAMESPACE_DECL

namespace {
struct ProviderFailure {};
class ThrowingFile : public File {
public:
  bool fail = true;
  unsigned reads = 0, writes = 0;
  uint8_t buffer[8]{};

  static FileIOResult read(File *base, void *data, size_t size) {
    auto *file = static_cast<ThrowingFile *>(base);
    ++file->reads;
    if (file->fail)
      throw ProviderFailure{};
    if (size)
      *static_cast<char *>(data) = '7';
    return size ? 1 : 0;
  }
  static FileIOResult write(File *base, const void *, size_t size) {
    auto *file = static_cast<ThrowingFile *>(base);
    ++file->writes;
    if (file->fail)
      throw ProviderFailure{};
    return size;
  }
  ThrowingFile(bool buffered = false)
      : File(write, read, nullptr, nullptr, buffer, sizeof(buffer),
             buffered ? _IOFBF : _IONBF, false,
             static_cast<ModeFlags>(OpenMode::READ) |
                 static_cast<ModeFlags>(OpenMode::WRITE)) {}
};

int print(FILE *file, const char *format, ...) {
  va_list args;
  va_start(args, format);
  try {
    int result = LIBC_NAMESPACE::vfprintf(file, format, args);
    va_end(args);
    return result;
  } catch (...) {
    va_end(args);
    throw;
  }
}
int scan(FILE *file, const char *format, ...) {
  va_list args;
  va_start(args, format);
  try {
    int result = LIBC_NAMESPACE::vfscanf(file, format, args);
    va_end(args);
    return result;
  } catch (...) {
    va_end(args);
    throw;
  }
}

void *acquire(void *data) {
  auto *file = static_cast<File *>(data);
  file->lock();
  file->unlock();
  File::lock_list();
  File::unlock_list();
  return data;
}
void check_unlocked(File &file) {
  // A same-thread acquisition would miss a leaked recursive stream lock.
  pthread_t thread;
  ASSERT_EQ(::pthread_create(&thread, nullptr, acquire, &file), 0);
  void *result = nullptr;
  ASSERT_EQ(::pthread_join(thread, &result), 0);
  ASSERT_EQ(result, static_cast<void *>(&file));
}
} // namespace

TEST(LlvmLibcStdioUnwindLockTest, FormattedOutput) {
  ThrowingFile file;
  bool caught = false;
  try {
    print(reinterpret_cast<FILE *>(&file), "%s", "output");
  } catch (ProviderFailure) {
    caught = true;
  }
  ASSERT_TRUE(caught);
  EXPECT_EQ(file.writes, 1U);
  check_unlocked(file);
  file.fail = false;
  EXPECT_EQ(print(reinterpret_cast<FILE *>(&file), "%s", "output"), 6);
  check_unlocked(file);
}

TEST(LlvmLibcStdioUnwindLockTest, FormattedInput) {
  ThrowingFile file;
  char value = 0;
  bool caught = false;
  try {
    scan(reinterpret_cast<FILE *>(&file), "%c", &value);
  } catch (ProviderFailure) {
    caught = true;
  }
  ASSERT_TRUE(caught);
  EXPECT_EQ(file.reads, 1U);
  check_unlocked(file);
  file.fail = false;
  EXPECT_EQ(scan(reinterpret_cast<FILE *>(&file), "%c", &value), 1);
  EXPECT_EQ(value, '7');
  check_unlocked(file);
}

TEST(LlvmLibcStdioUnwindLockTest, GlobalFlush) {
  ThrowingFile file(true);
  ASSERT_EQ(file.write_unlocked("data", 4).value, size_t(4));
  File::add_file(&file);
  bool caught = false;
  try {
    LIBC_NAMESPACE::fflush(nullptr);
  } catch (ProviderFailure) {
    caught = true;
  }
  ASSERT_TRUE(caught);
  EXPECT_EQ(file.writes, 1U);
  check_unlocked(file);
  file.fail = false;
  EXPECT_EQ(LIBC_NAMESPACE::fflush(nullptr), 0);
  check_unlocked(file);
  File::remove_file(&file);
}

namespace {
class ProgressThenUnwindFile : public File {
  uint8_t buffer[8]{};
  static FileIOResult write_bytes(File *base, const void *data, size_t size) {
    auto *file = static_cast<ProgressThenUnwindFile *>(base);
    if (++file->calls == 2 && file->fail)
      throw ProviderFailure{};
    size_t count = file->calls == 1 && size > 2 ? 2 : size;
    for (size_t i = 0; i < count; ++i)
      file->output[file->written++] = static_cast<const char *>(data)[i];
    return count;
  }
  static int close_file(File *) { return 0; }

public:
  char output[32]{};
  size_t written = 0;
  unsigned calls = 0;
  bool fail = true;
  ProgressThenUnwindFile()
      : File(write_bytes, nullptr, nullptr, close_file, buffer, sizeof(buffer),
             _IOFBF, false, mode_flags("w")) {}
};
} // namespace

TEST(LlvmLibcStdioUnwindLockTest, PartialOutputSurvivesLaterUnwind) {
  LocaleGuard locale;
  for (unsigned kind = 0; kind != 3; ++kind) {
    ProgressThenUnwindFile file;
    if (kind == 2)
      ASSERT_EQ(file.write(L"\u20acX", 2).value, size_t(2));
    else
      ASSERT_EQ(file.write("abcdef", 6).value, size_t(6));
    bool caught = false;
    try {
      if (kind == 1)
        file.write("ghij", 4);
      else
        file.flush();
    } catch (ProviderFailure) {
      caught = true;
    }
    ASSERT_TRUE(caught);
    EXPECT_EQ(file.calls, 2U);
    EXPECT_EQ(file.written, size_t(2));
    check_unlocked(file);
    file.fail = false;
    ASSERT_EQ(file.flush(), 0);
    const char *expected = kind == 2 ? "\xe2\x82\xacX" : "abcdef";
    size_t length = kind == 2 ? 4 : 6;
    EXPECT_EQ(file.written, length);
    for (size_t i = 0; i != length; ++i)
      EXPECT_EQ(file.output[i], expected[i]);
    EXPECT_EQ(file.close(), 0);
  }
}

TEST(LlvmLibcStdioUnwindLockTest, LineBufferedGlobalFlush) {
  ThrowingFile file(true);
  ASSERT_EQ(file.set_buffer(file.buffer, sizeof(file.buffer), _IOLBF), 0);
  ASSERT_EQ(file.write_unlocked("data", 4).value, size_t(4));
  File::add_file(&file);
  bool caught = false;
  try {
    LIBC_NAMESPACE::_flushlbf();
  } catch (ProviderFailure) {
    caught = true;
  }
  ASSERT_TRUE(caught);
  EXPECT_EQ(file.writes, 1U);
  check_unlocked(file);
  file.fail = false;
  LIBC_NAMESPACE::_flushlbf();
  EXPECT_EQ(file.pending_output(), size_t(0));
  check_unlocked(file);
  File::remove_file(&file);
}

TEST(LlvmLibcStdioUnwindLockTest, InputTriggeredFlush) {
  ThrowingFile output(true), input;
  ASSERT_EQ(output.set_buffer(output.buffer, sizeof(output.buffer), _IOLBF), 0);
  ASSERT_EQ(output.write_unlocked("data", 4).value, size_t(4));
  File::add_file(&output);
  input.fail = false;
  bool caught = false;
  char byte;
  try {
    input.File::read(&byte, 1);
  } catch (ProviderFailure) {
    caught = true;
  }
  ASSERT_TRUE(caught);
  EXPECT_EQ(output.writes, 1U);
  EXPECT_EQ(input.reads, 0U);
  check_unlocked(input);
  check_unlocked(output);
  output.fail = false;
  EXPECT_EQ(input.File::read(&byte, 1).value, size_t(1));
  EXPECT_EQ(byte, '7');
  EXPECT_EQ(output.pending_output(), size_t(0));
  File::remove_file(&output);
}

TEST(LlvmLibcStdioUnwindLockTest, CloseDuringLineBufferedFlush) {
  struct BlockingFile : File {
    uint8_t buffer[8]{};
    LIBC_NAMESPACE::cpp::Atomic<bool> entered{false}, release{false},
        closing{false};
    bool written = false, closed_after_write = false;
    static FileIOResult write(File *base, const void *, size_t size) {
      auto &f = *static_cast<BlockingFile *>(base);
      f.entered.store(true);
      while (!f.release.load()) {}
      f.written = true;
      return size;
    }
    static int close_callback(File *base) {
      auto &f = *static_cast<BlockingFile *>(base);
      f.closed_after_write = f.written;
      return 0;
    }
    BlockingFile()
        : File(write, nullptr, nullptr, close_callback, buffer, sizeof(buffer),
               _IOLBF, false, mode_flags("w")) {}
  } file;
  ASSERT_EQ(file.write_unlocked("data", 4).value, size_t(4));
  File::add_file(&file);
  pthread_t flusher, closer;
  ASSERT_EQ(::pthread_create(&flusher, nullptr, [](void *) -> void * {
    LIBC_NAMESPACE::_flushlbf();
    return nullptr;
  }, nullptr), 0);
  while (!file.entered.load()) {}
  ASSERT_EQ(::pthread_create(&closer, nullptr, [](void *data) -> void * {
    auto &f = *static_cast<BlockingFile *>(data);
    f.closing.store(true);
    f.close(false);
    return nullptr;
  }, &file), 0);
  while (!file.closing.load()) {}
  file.release.store(true);
  ASSERT_EQ(::pthread_join(flusher, nullptr), 0);
  ASSERT_EQ(::pthread_join(closer, nullptr), 0);
  EXPECT_TRUE(file.closed_after_write);
  EXPECT_EQ(File::get_first_file(), static_cast<File *>(nullptr));
  check_unlocked(file);
}

TEST(LlvmLibcStdioUnwindLockTest, CancellationDuringLineBufferedFlush) {
  struct CancelFile : File {
    uint8_t buffer[8]{};
    LIBC_NAMESPACE::cpp::Atomic<bool> entered{false};
    static FileIOResult write(File *base, const void *, size_t) {
      static_cast<CancelFile *>(base)->entered.store(true);
      for (;;)
        ::pthread_testcancel();
    }
    CancelFile()
        : File(write, nullptr, nullptr, nullptr, buffer, sizeof(buffer),
               _IOLBF, false, mode_flags("w")) {}
  } file;
  ASSERT_EQ(file.write_unlocked("data", 4).value, size_t(4));
  File::add_file(&file);
  pthread_t thread;
  ASSERT_EQ(::pthread_create(&thread, nullptr, [](void *) -> void * {
    LIBC_NAMESPACE::_flushlbf();
    return nullptr;
  }, nullptr), 0);
  while (!file.entered.load()) {}
  ASSERT_EQ(::pthread_cancel(thread), 0);
  void *result = nullptr;
  ASSERT_EQ(::pthread_join(thread, &result), 0);
  EXPECT_EQ(result, PTHREAD_CANCELED);
  check_unlocked(file);
  EXPECT_EQ(file.pending_output(), size_t(4));
  file.purge_unlocked();
  File::remove_file(&file);
}
