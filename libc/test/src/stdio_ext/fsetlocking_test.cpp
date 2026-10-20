//===-- Tests for stream locking control ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "include/llvm-libc-macros/stdio-ext-macros.h"
#include "src/__support/File/file.h"
#include "src/stdio/flockfile.h"
#include "src/stdio/funlockfile.h"
#include "src/stdio/vfprintf.h"
#include "src/stdio/vfscanf.h"
#include "src/stdio_ext/__fsetlocking.h"
#include "test/UnitTest/Test.h"
#include <stdarg.h>
#ifdef LIBC_FULL_BUILD
#include "src/pthread/pthread_create.h"
#include "src/pthread/pthread_join.h"
#else
#include <pthread.h>
#endif

namespace LIBC_NAMESPACE_DECL {
FILE *stdin = nullptr;
FILE *stdout = nullptr;
FILE *stderr = nullptr;
} // namespace LIBC_NAMESPACE_DECL

using LIBC_NAMESPACE::File;
using LIBC_NAMESPACE::FileIOResult;

namespace {
namespace thread_api {
#ifdef LIBC_FULL_BUILD
using LIBC_NAMESPACE::pthread_create;
using LIBC_NAMESPACE::pthread_join;
#else
using ::pthread_create;
using ::pthread_join;
#endif
} // namespace thread_api
struct Probe {
  File *file;
  bool acquired = false;
};
void *try_lock(void *data) {
  auto &probe = *static_cast<Probe *>(data);
  probe.acquired = probe.file->try_lock();
  if (probe.acquired)
    probe.file->unlock();
  return nullptr;
}
bool available(File &file) {
  Probe probe{&file};
  pthread_t thread;
  int error = thread_api::pthread_create(&thread, nullptr, try_lock, &probe);
  EXPECT_EQ(error, 0);
  if (error)
    return false;
  EXPECT_EQ(thread_api::pthread_join(thread, nullptr), 0);
  return probe.acquired;
}
class Stream : public File {
  uint8_t buffer[8]{};

public:
  bool observed_unlocked = false;
  static FileIOResult write_callback(File *f, const void *, size_t size) {
    auto *stream = static_cast<Stream *>(f);
    stream->observed_unlocked = available(*stream);
    return size;
  }
  static FileIOResult read_callback(File *f, void *data, size_t size) {
    auto *stream = static_cast<Stream *>(f);
    stream->observed_unlocked = available(*stream);
    if (size)
      *static_cast<char *>(data) = 'x';
    return size ? 1 : 0;
  }
  Stream()
      : File(write_callback, read_callback, nullptr, nullptr, buffer, sizeof(buffer), _IONBF,
             false, static_cast<ModeFlags>(OpenMode::READ) |
                        static_cast<ModeFlags>(OpenMode::WRITE)) {}
  FILE *handle() { return reinterpret_cast<FILE *>(this); }
};
int print(FILE *stream, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = LIBC_NAMESPACE::vfprintf(stream, format, args);
  va_end(args);
  return result;
}
int scan(FILE *stream, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = LIBC_NAMESPACE::vfscanf(stream, format, args);
  va_end(args);
  return result;
}
} // namespace

TEST(LlvmLibcFsetlockingTest, ModesAndAutomaticIO) {
  Stream output, input;
  const int modes[] = {FSETLOCKING_INTERNAL, FSETLOCKING_BYCALLER,
                       FSETLOCKING_INTERNAL};
  Stream *streams[] = {&output, &input};
  for (int mode : modes) {
    for (Stream *stream : streams) {
      int previous =
          LIBC_NAMESPACE::__fsetlocking(stream->handle(), FSETLOCKING_QUERY);
      EXPECT_EQ(LIBC_NAMESPACE::__fsetlocking(stream->handle(), mode), previous);
      EXPECT_EQ(LIBC_NAMESPACE::__fsetlocking(stream->handle(), FSETLOCKING_QUERY),
                mode);
    }
    EXPECT_EQ(output.write("x", 1).value, size_t(1));
    EXPECT_EQ(output.observed_unlocked, mode == FSETLOCKING_BYCALLER);
    EXPECT_EQ(print(output.handle(), "%s", "x"), 1);
    EXPECT_EQ(output.observed_unlocked, mode == FSETLOCKING_BYCALLER);
    char value;
    EXPECT_EQ(scan(input.handle(), "%c", &value), 1);
    EXPECT_EQ(value, 'x');
    EXPECT_EQ(input.observed_unlocked, mode == FSETLOCKING_BYCALLER);
    EXPECT_TRUE(available(output));
    EXPECT_TRUE(available(input));
  }
}

TEST(LlvmLibcFsetlockingTest, ExplicitRecursiveLocksRemainEffective) {
  Stream stream;
  EXPECT_EQ(LIBC_NAMESPACE::__fsetlocking(stream.handle(), FSETLOCKING_BYCALLER),
            FSETLOCKING_INTERNAL);
  LIBC_NAMESPACE::flockfile(stream.handle());
  LIBC_NAMESPACE::flockfile(stream.handle());
  EXPECT_FALSE(available(stream));
  EXPECT_EQ(print(stream.handle(), "%s", "x"), 1);
  EXPECT_FALSE(stream.observed_unlocked);
  LIBC_NAMESPACE::funlockfile(stream.handle());
  EXPECT_FALSE(available(stream));
  LIBC_NAMESPACE::funlockfile(stream.handle());
  EXPECT_TRUE(available(stream));
}

TEST(LlvmLibcFsetlockingTest, GuardRemembersAcquisition) {
  Stream stream;
  {
    File::FileLock guard(&stream);
    EXPECT_EQ(
        LIBC_NAMESPACE::__fsetlocking(stream.handle(), FSETLOCKING_BYCALLER),
        FSETLOCKING_INTERNAL);
    EXPECT_FALSE(available(stream));
  }
  EXPECT_TRUE(available(stream));
  {
    File::FileLock guard(&stream);
    EXPECT_EQ(
        LIBC_NAMESPACE::__fsetlocking(stream.handle(), FSETLOCKING_INTERNAL),
        FSETLOCKING_BYCALLER);
  }
  EXPECT_TRUE(available(stream));
}
