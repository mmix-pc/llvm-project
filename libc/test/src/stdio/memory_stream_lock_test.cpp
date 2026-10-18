//===-- Memory stream locking tests ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/func/free.h"
#include "src/stdio/fclose.h"
#include "src/stdio/fflush.h"
#include "src/stdio/flockfile.h"
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
} // namespace

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
