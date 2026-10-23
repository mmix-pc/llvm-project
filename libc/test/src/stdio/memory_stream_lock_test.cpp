//===-- Memory stream locking tests ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/func/free.h"
#include "src/__support/File/file.h"
#include "src/stdio/fclose.h"
#include "src/stdio/fflush.h"
#include "src/stdio/flockfile.h"
#include "src/stdio/ftrylockfile.h"
#include "src/stdio/funlockfile.h"
#include "src/stdio/fwrite.h"
#include "src/stdio/open_memstream.h"
#include "test/UnitTest/Test.h"
#include <pthread.h>

// Keep host standard streams out of the LLVM File registry under test.
namespace LIBC_NAMESPACE_DECL {
FILE *stdin = nullptr;
FILE *stdout = nullptr;
FILE *stderr = nullptr;
} // namespace LIBC_NAMESPACE_DECL

namespace LIBC = LIBC_NAMESPACE;
namespace {
struct Writer {
  FILE *stream;
  const char *record;
};

void *write_records(void *arg) {
  auto *writer = static_cast<Writer *>(arg);
  for (unsigned i = 0; i != 100; ++i) {
    // Both inner operations must also work while the recursive stream lock
    // is held explicitly. Different writers must not interleave records.
    LIBC::flockfile(writer->stream);
    bool success = LIBC::fwrite(writer->record, 1, 4, writer->stream) == 4 &&
                   LIBC::fflush(writer->stream) == 0;
    LIBC::funlockfile(writer->stream);
    if (!success)
      return arg;
  }
  return nullptr;
}
void *try_lock(void *arg) {
  auto *stream = static_cast<FILE *>(arg);
  if (LIBC::ftrylockfile(stream) != 0)
    return nullptr;
  LIBC::funlockfile(stream);
  return arg;
}

void *read_one(void *arg) {
  auto *input = static_cast<LIBC::File *>(arg);
  char byte;
  return input->read(&byte, 1).value == 1 && byte == 'r' ? nullptr : arg;
}
} // namespace

TEST(LlvmLibcMemoryStreamLockTest, InputSkipsBusyOutputAndRegistry) {
  auto read = [](LIBC::File *, void *buffer,
                 size_t size) -> LIBC::FileIOResult {
    if (size)
      *static_cast<char *>(buffer) = 'r';
    return size ? size_t(1) : size_t(0);
  };
  LIBC::File input(nullptr, read, nullptr, nullptr, nullptr, 0, _IONBF, false,
                   LIBC::File::mode_flags("r"));
  char *memory;
  size_t length;
  FILE *f = LIBC::open_memstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  auto *output = reinterpret_cast<LIBC::File *>(f);
  char buffer[8];
  ASSERT_EQ(output->set_buffer(buffer, sizeof(buffer), _IOLBF), 0);
  ASSERT_EQ(LIBC::fwrite("x", 1, 1, f), size_t(1));
  // The worker must not wait for a lock held by the thread joining it.
  const bool locks[] = {false, true};
  for (bool registry : locks) {
    if (registry)
      LIBC::File::lock_list();
    else
      LIBC::flockfile(f);
    pthread_t thread;
    int created = ::pthread_create(&thread, nullptr, read_one, &input);
    void *result = &input;
    int joined = created == 0 ? ::pthread_join(thread, &result) : -1;
    if (registry)
      LIBC::File::unlock_list();
    else
      LIBC::funlockfile(f);
    ASSERT_EQ(created, 0);
    EXPECT_EQ(joined, 0);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(output->pending_output(), size_t(1));
  }
  EXPECT_EQ(read_one(&input), nullptr);
  EXPECT_EQ(length, size_t(1));
  EXPECT_EQ(memory[0], 'x');
  EXPECT_EQ(LIBC::fclose(f), 0);
  ::free(memory);
}

TEST(LlvmLibcMemoryStreamLockTest, TryLockPreservesRecursiveOwnership) {
  char *memory;
  size_t length;
  FILE *f = LIBC::open_memstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  LIBC::flockfile(f);
  ASSERT_EQ(LIBC::ftrylockfile(f), 0);
  ASSERT_EQ(LIBC::fwrite("x", 1, 1, f), size_t(1));
  for (unsigned depth = 2; depth; --depth) {
    pthread_t thread;
    ASSERT_EQ(::pthread_create(&thread, nullptr, try_lock, f), 0);
    void *result;
    ASSERT_EQ(::pthread_join(thread, &result), 0);
    EXPECT_EQ(result, nullptr);
    LIBC::funlockfile(f);
  }
  pthread_t thread;
  ASSERT_EQ(::pthread_create(&thread, nullptr, try_lock, f), 0);
  void *result;
  ASSERT_EQ(::pthread_join(thread, &result), 0);
  EXPECT_EQ(result, static_cast<void *>(f));
  EXPECT_EQ(LIBC::fclose(f), 0);
  ::free(memory);
}

TEST(LlvmLibcMemoryStreamLockTest, ConcurrentGrowthAndPublication) {
  char *memory;
  size_t length;
  FILE *f = LIBC::open_memstream(&memory, &length);
  ASSERT_NE(f, nullptr);
  Writer writers[] = {{f, "aaaa"}, {f, "bbbb"}};
  pthread_t threads[2];
  for (unsigned i = 0; i != 2; ++i)
    ASSERT_EQ(
        ::pthread_create(&threads[i], nullptr, write_records, &writers[i]), 0);
  for (auto &thread : threads) {
    void *result;
    ASSERT_EQ(::pthread_join(thread, &result), 0);
    EXPECT_EQ(result, nullptr);
  }
  ASSERT_EQ(LIBC::fclose(f), 0);
  ASSERT_EQ(length, size_t(800));
  unsigned counts[2] = {};
  for (size_t i = 0; i != length; i += 4) {
    ASSERT_TRUE(memory[i] == 'a' || memory[i] == 'b');
    ++counts[memory[i] == 'b'];
    for (size_t j = 1; j != 4; ++j)
      EXPECT_EQ(memory[i + j], memory[i]);
  }
  EXPECT_EQ(counts[0], 100U);
  EXPECT_EQ(counts[1], 100U);
  EXPECT_EQ(memory[length], '\0');
  ::free(memory);
}
