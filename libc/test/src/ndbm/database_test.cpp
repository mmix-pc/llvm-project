//===-- Tests for the private file-backed DBM engine -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/ndbm/linux/database.h"
#include "test/UnitTest/Test.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace db = LIBC_NAMESPACE::dbm;
namespace {
unsigned fail_at, allocations, releases;
bool track;
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (track && ++allocations == fail_at)
    return nullptr;
  return __real_malloc(size);
}
extern "C" void __wrap_free(void *pointer) {
  if (track && pointer)
    ++releases;
  __real_free(pointer);
}

TEST(LlvmLibcDatabaseTest, LifecycleAndOpenFailuresReleaseOwnedStorage) {
  auto base = libc_make_test_file_path("database-owned");
  auto name = libc_make_test_file_path("database-owned.db");
  ::unlink(name);
  const unsigned char key[] = {0, 3}, value[] = {0xff, 0};
  const db::HashRecord record{key, sizeof(key), value, sizeof(value)};
  {
    db::Database initial;
    ASSERT_TRUE(initial.open(base, O_CREAT | O_RDWR, 0600).has_value());
    ASSERT_TRUE(initial.store(record, false).has_value());
  }
  unsigned open_allocations = 0;
  // Include every validation allocation and the successful cursor lifecycle.
  for (unsigned site = 0; site <= open_allocations; ++site) {
    allocations = releases = 0;
    fail_at = site;
    int error = 0;
    bool observed = false;
    track = true;
    {
      db::Database database;
      auto opened = database.open(base, O_RDWR, 0);
      if (!opened) {
        error = opened.error();
      } else {
        if (site == 0)
          open_allocations = allocations;
        db::HashRecord found{};
        auto first = database.first(found);
        observed = first && *first && found.key_size == sizeof(key) &&
                   found.key[0] == key[0] && found.key[1] == key[1];
      }
    }
    track = false;
    if (site) {
      EXPECT_EQ(error, ENOMEM);
      EXPECT_EQ(allocations, releases + 1);
    } else {
      EXPECT_EQ(error, 0);
      EXPECT_TRUE(observed);
      EXPECT_EQ(allocations, releases);
      EXPECT_GT(open_allocations, 0U);
    }
  }
  ::unlink(name);
}

TEST(LlvmLibcDatabaseTest, BinaryReplacementDeletionAndBorrowedInput) {
  auto base = libc_make_test_file_path("database-records");
  auto name = libc_make_test_file_path("database-records.db");
  ::unlink(name);
  db::Database database;
  ASSERT_TRUE(database.open(base, O_CREAT | O_RDWR, 0600).has_value());
  const unsigned char key[] = {0, 0xff}, value[] = {0x80, 0, 0x7f};
  db::HashRecord record{key, sizeof(key), value, sizeof(value)}, out{};
  auto stored = database.store(record, false);
  ASSERT_TRUE(stored.has_value());
  EXPECT_TRUE(*stored);
  record.content_size = 1;
  stored = database.store(record, false);
  ASSERT_TRUE(stored.has_value());
  EXPECT_FALSE(*stored);
  EXPECT_EQ(database.error(), 0);
  auto fetched = database.fetch(key, sizeof(key), out);
  ASSERT_TRUE(fetched.has_value());
  ASSERT_TRUE(*fetched);
  EXPECT_EQ(out.content_size, sizeof(value));
  for (size_t i = 0; i < sizeof(value); ++i)
    EXPECT_EQ(out.content[i], value[i]);
  // Both inputs may borrow the result of the immediately preceding fetch.
  out.content_size = 1;
  stored = database.store(out, true);
  ASSERT_TRUE(stored.has_value());
  EXPECT_TRUE(*stored);
  ASSERT_TRUE(database.close().has_value());
  ASSERT_TRUE(database.open(base, O_RDWR, 0).has_value());
  fetched = database.fetch(key, sizeof(key), out);
  ASSERT_TRUE(fetched.has_value());
  ASSERT_TRUE(*fetched);
  EXPECT_EQ(out.content_size, size_t{1});
  EXPECT_EQ(out.content[0], value[0]);
  auto deleted = database.erase(key, sizeof(key));
  ASSERT_TRUE(deleted.has_value());
  EXPECT_TRUE(*deleted);
  deleted = database.erase(key, sizeof(key));
  ASSERT_TRUE(deleted.has_value());
  EXPECT_FALSE(*deleted);
  fetched = database.fetch(key, sizeof(key), out);
  ASSERT_TRUE(fetched.has_value());
  EXPECT_FALSE(*fetched);
  EXPECT_EQ(out.key, nullptr);
  EXPECT_EQ(out.content, nullptr);
  EXPECT_EQ(database.error(), 0);
  ASSERT_TRUE(database.close().has_value());
  ASSERT_TRUE(database.open(base, O_RDONLY, 0).has_value());
  auto first = database.first(out);
  ASSERT_TRUE(first.has_value());
  EXPECT_FALSE(*first);
  ::unlink(name);
}

TEST(LlvmLibcDatabaseTest, EmptyRecordsAndMaximumRecords) {
  auto base = libc_make_test_file_path("database-limits");
  auto name = libc_make_test_file_path("database-limits.db");
  ::unlink(name);
  db::Database database;
  ASSERT_TRUE(
      database.open(base, O_CREAT | O_RDWR | O_DSYNC, 0600).has_value());
  db::HashRecord empty{nullptr, 0, nullptr, 0}, out{};
  ASSERT_TRUE(database.store(empty, false).has_value());
  auto fetched = database.fetch(nullptr, 0, out);
  ASSERT_TRUE(fetched.has_value());
  ASSERT_TRUE(*fetched);
  EXPECT_NE(out.content, nullptr);
  EXPECT_EQ(out.content_size, size_t{0});
  unsigned char value[db::HASH_RECORD_LIMIT]{};
  for (size_t i = 0; i < sizeof(value); ++i)
    value[i] = static_cast<unsigned char>(i);
  db::HashRecord large{value, 1023, value + 1023, sizeof(value) - 1023};
  ASSERT_TRUE(database.store(large, false).has_value());
  ++large.content_size;
  auto oversized = database.store(large, true);
  ASSERT_FALSE(oversized.has_value());
  EXPECT_EQ(oversized.error(), EINVAL);
  EXPECT_EQ(database.error(), EINVAL);
  database.clear_error();
  EXPECT_EQ(database.error(), 0);
  ASSERT_TRUE(database.close().has_value());
  ASSERT_TRUE(database.open(base, O_RDONLY, 0).has_value());
  fetched = database.fetch(value, 1023, out);
  ASSERT_TRUE(fetched.has_value());
  ASSERT_TRUE(*fetched);
  EXPECT_EQ(out.content_size, sizeof(value) - 1023);
  for (size_t i = 0; i < out.content_size; ++i)
    EXPECT_EQ(out.content[i], value[i + 1023]);
  ::unlink(name);
}

TEST(LlvmLibcDatabaseTest, GrowthIterationAndReadonlyErrors) {
  auto base = libc_make_test_file_path("database-growth");
  auto name = libc_make_test_file_path("database-growth.db");
  ::unlink(name);
  db::Database database;
  ASSERT_TRUE(database.open(base, O_CREAT | O_RDWR, 0600).has_value());
  for (unsigned i = 0; i < 257; ++i) {
    unsigned char key[] = {static_cast<unsigned char>(i >> 8),
                           static_cast<unsigned char>(i)};
    db::HashRecord record{key, sizeof(key), key, sizeof(key)};
    auto inserted = database.store(record, false);
    ASSERT_TRUE(inserted.has_value());
    EXPECT_TRUE(*inserted);
  }
  ASSERT_TRUE(database.close().has_value());
  ASSERT_TRUE(database.open(base, O_RDONLY, 0).has_value());
  bool seen[257]{};
  size_t count = 0;
  db::HashRecord out{};
  auto next = database.first(out);
  for (;;) {
    ASSERT_TRUE(next.has_value());
    if (!*next)
      break;
    ASSERT_EQ(out.key_size, size_t{2});
    unsigned index = unsigned(out.key[0]) * 256 + out.key[1];
    ASSERT_LT(index, 257U);
    EXPECT_FALSE(seen[index]);
    seen[index] = true;
    ++count;
    next = database.next(out);
  }
  EXPECT_EQ(count, size_t{257});
  EXPECT_EQ(out.key, nullptr);
  EXPECT_EQ(out.key_size, size_t{0});
  auto denied = database.erase(nullptr, 0);
  ASSERT_FALSE(denied.has_value());
  EXPECT_EQ(denied.error(), EBADF);
  database.clear_error();
  ASSERT_TRUE(database.close().has_value());
  ASSERT_TRUE(database.open(base, O_WRONLY, 0).has_value());
  auto fetched = database.fetch(nullptr, 0, out);
  ASSERT_FALSE(fetched.has_value());
  EXPECT_EQ(fetched.error(), EBADF);
  ::unlink(name);
}

TEST(LlvmLibcDatabaseTest, EveryReplacementPreparationFailurePreservesValue) {
  auto base = libc_make_test_file_path("database-failures");
  auto name = libc_make_test_file_path("database-failures.db");
  ::unlink(name);
  db::Database database;
  ASSERT_TRUE(database.open(base, O_CREAT | O_RDWR, 0600).has_value());
  unsigned char key = 2, old = 11, replacement = 22;
  db::HashRecord record{&key, 1, &old, 1}, out{};
  ASSERT_TRUE(database.store(record, false).has_value());
  record.content = &replacement;
  // A one-record replacement allocates the snapshot's two buffers and one
  // staged primary page; complete validation needs no duplicate-key workspace.
  for (unsigned site = 1; site <= 3; ++site) {
    allocations = 0;
    fail_at = site;
    track = true;
    auto failed = database.store(record, true);
    track = false;
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error(), ENOMEM);
    database.clear_error();
    auto fetched = database.fetch(&key, 1, out);
    ASSERT_TRUE(fetched.has_value());
    ASSERT_TRUE(*fetched);
    EXPECT_EQ(out.content[0], old);
    ASSERT_TRUE(database.close().has_value());
    ASSERT_TRUE(database.open(base, O_RDWR, 0).has_value());
  }
  auto stored = database.store(record, true);
  ASSERT_TRUE(stored.has_value());
  EXPECT_TRUE(*stored);
  ::unlink(name);
}

TEST(LlvmLibcDatabaseTest, FailedPublicationRequiresReopenAfterClearError) {
  auto base = libc_make_test_file_path("database-write-failure");
  auto name = libc_make_test_file_path("database-write-failure.db");
  ::unlink(name);
  pid_t child = ::fork();
  ASSERT_GE(child, pid_t{0});
  if (!child) {
    db::Database database;
    if (!database.open(base, O_CREAT | O_RDWR, 0600))
      _exit(1);
    unsigned char key = 2, old = 11, replacement = 22;
    db::HashRecord record{&key, 1, &old, 1}, out{};
    if (!database.store(record, false))
      _exit(2);
    if (::signal(SIGXFSZ, SIG_IGN) == SIG_ERR)
      _exit(3);
    struct rlimit original, limited;
    if (::getrlimit(RLIMIT_FSIZE, &original))
      _exit(4);
    limited = original;
    limited.rlim_cur = 0;
    if (::setrlimit(RLIMIT_FSIZE, &limited))
      _exit(5);
    record.content = &replacement;
    auto failed = database.store(record, true);
    if (failed || failed.error() != EFBIG || database.error() != EFBIG)
      _exit(6);
    database.clear_error();
    if (database.error())
      _exit(7);
    auto fetched = database.fetch(&key, 1, out);
    if (fetched || fetched.error() != EIO)
      _exit(8);
    if (::setrlimit(RLIMIT_FSIZE, &original) || !database.close() ||
        !database.open(base, O_RDWR, 0))
      _exit(9);
    fetched = database.fetch(&key, 1, out);
    if (!fetched || !*fetched || out.content_size != 1 || out.content[0] != old)
      _exit(10);
    if (!database.close())
      _exit(11);
    _exit(0);
  }
  int status;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
  ::unlink(name);
}
