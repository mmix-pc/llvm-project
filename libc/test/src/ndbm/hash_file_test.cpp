//===-- Tests for BSD hash file ownership ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/ndbm/hash_rebuild.h"
#include "src/ndbm/linux/hash_file.h"
#include "test/UnitTest/Test.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace db = LIBC_NAMESPACE::dbm;

namespace {
bool fail_allocation;
}
extern "C" void *__real_malloc(size_t);
extern "C" void *__wrap_malloc(size_t size) {
  if (fail_allocation)
    return nullptr;
  return __real_malloc(size);
}

TEST(LlvmLibcHashFileTest, ReadonlyCreateAndReopenEmpty) {
  auto base = libc_make_test_file_path("hash-readonly");
  auto name = libc_make_test_file_path("hash-readonly.db");
  ::unlink(name);
  db::HashFile file;
  auto opened = file.open(base, O_RDONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  ASSERT_TRUE(opened.has_value());
  EXPECT_EQ(opened->extent, uint64_t{0});
  EXPECT_TRUE(file.readable());
  EXPECT_FALSE(file.writable());
  EXPECT_EQ(::fcntl(file.descriptor(), F_GETFL) & O_ACCMODE, O_RDONLY);
  EXPECT_EQ(::fcntl(file.descriptor(), F_GETFD) & FD_CLOEXEC, FD_CLOEXEC);
  struct stat status;
  ASSERT_EQ(::fstat(file.descriptor(), &status), 0);
  EXPECT_EQ(status.st_size, off_t{0});
  EXPECT_EQ(status.st_mode & 0777, mode_t{0600});
  auto again = file.open(base, O_RDONLY, 0);
  ASSERT_FALSE(again.has_value());
  EXPECT_EQ(again.error(), EBUSY);
  ASSERT_TRUE(file.close().has_value());
  ASSERT_TRUE(file.open(base, O_RDONLY, 0).has_value());
  unsigned char byte = 0;
  auto written = file.io().write_at(file.io().context, &byte, 1, 0);
  ASSERT_FALSE(written.has_value());
  EXPECT_EQ(written.error(), EBADF);
  ::unlink(name);
}

TEST(LlvmLibcHashFileTest, PublishAndReopenBinaryRecord) {
  auto base = libc_make_test_file_path("hash-persistent");
  auto name = libc_make_test_file_path("hash-persistent.db");
  ::unlink(name);
  db::HashFile file;
  auto opened = file.open(base, O_RDWR | O_CREAT | O_EXCL | O_SYNC, 0600);
  ASSERT_TRUE(opened.has_value());
  ASSERT_NE(file.synchronizer(), nullptr);
  db::HashWriteBatch batch(file.io(), opened->extent);
  ASSERT_TRUE(db::HashFile::prepare_empty(batch).has_value());
  const unsigned char key[] = {0, 2}, content[] = {0xff, 0, 0x80};
  db::HashRecord record{key, sizeof(key), content, sizeof(content)};
  auto header = db::rebuild_hash_bucket(batch, opened->header, opened->header,
                                        0, &record, 1);
  ASSERT_TRUE(header.has_value());
  ASSERT_TRUE(batch.publish(*header, file.synchronizer()).has_value());
  struct stat before;
  ASSERT_EQ(::fstat(file.descriptor(), &before), 0);
  ASSERT_TRUE(file.close().has_value());
  opened = file.open(base, O_RDONLY, 0);
  ASSERT_TRUE(opened.has_value());
  EXPECT_EQ(opened->header.keys, uint32_t{1});
  EXPECT_EQ(opened->extent, uint64_t(4 * db::HASH_PAGE_SIZE));
  unsigned char magic[4];
  ASSERT_EQ(::pread(file.descriptor(), magic, sizeof(magic), 0), ssize_t{4});
  EXPECT_EQ(unsigned(magic[0]), 0U);
  EXPECT_EQ(unsigned(magic[1]), 6U);
  EXPECT_EQ(unsigned(magic[2]), 0x15U);
  EXPECT_EQ(unsigned(magic[3]), 0x61U);
  ASSERT_TRUE(file.close().has_value());
  auto exclusive = file.open(base, O_CREAT | O_EXCL | O_RDWR, 0644);
  ASSERT_FALSE(exclusive.has_value());
  EXPECT_EQ(exclusive.error(), EEXIST);
  ASSERT_TRUE(file.open(base, O_WRONLY | O_TRUNC, 0).has_value());
  EXPECT_FALSE(file.readable());
  EXPECT_TRUE(file.writable());
  EXPECT_EQ(::fcntl(file.descriptor(), F_GETFL) & O_ACCMODE, O_RDWR);
  struct stat after;
  ASSERT_EQ(::fstat(file.descriptor(), &after), 0);
  EXPECT_EQ(after.st_size, off_t{0});
  EXPECT_EQ(after.st_mode, before.st_mode);
  EXPECT_EQ(after.st_uid, before.st_uid);
  EXPECT_EQ(after.st_gid, before.st_gid);
  ASSERT_TRUE(file.close().has_value());
  opened = file.open(base, O_RDONLY, 0);
  ASSERT_TRUE(opened.has_value());
  EXPECT_EQ(opened->header.keys, uint32_t{0});
  ::unlink(name);
}

TEST(LlvmLibcHashFileTest, MalformedNonemptyFileIsNotReinitialized) {
  auto base = libc_make_test_file_path("hash-malformed");
  auto name = libc_make_test_file_path("hash-malformed.db");
  int raw = ::open(name, O_CREAT | O_TRUNC | O_RDWR, 0600);
  ASSERT_GE(raw, 0);
  const unsigned char bytes[] = {0x55, 0x81, 0x7f};
  ASSERT_EQ(::write(raw, bytes, sizeof(bytes)), ssize_t{3});
  db::HashFile file;
  auto failed = file.open(base, O_RDWR | O_CREAT, 0600);
  EXPECT_FALSE(failed.has_value());
  EXPECT_EQ(file.descriptor(), -1);
  struct stat status;
  ASSERT_EQ(::fstat(raw, &status), 0);
  EXPECT_EQ(status.st_size, off_t{3});
  unsigned char copy[3];
  ASSERT_EQ(::pread(raw, copy, sizeof(copy), 0), ssize_t{3});
  for (size_t i = 0; i < sizeof(copy); ++i)
    EXPECT_EQ(copy[i], bytes[i]);
  ::close(raw);
  ::unlink(name);
}

TEST(LlvmLibcHashFileTest, AdmissionAndAllocationFailureDoNotCreateFile) {
  auto base = libc_make_test_file_path("hash-failed");
  auto name = libc_make_test_file_path("hash-failed.db");
  ::unlink(name);
  db::HashFile file;
  const int invalid[] = {O_RDWR | O_APPEND, O_RDONLY | O_TRUNC, O_ACCMODE};
  for (int flags : invalid) {
    auto failed = file.open(base, flags | O_CREAT, 0600);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error(), EINVAL);
  }
  auto no_create = file.open(base, O_EXCL | O_RDWR, 0600);
  ASSERT_FALSE(no_create.has_value());
  EXPECT_EQ(no_create.error(), EINVAL);
  fail_allocation = true;
  auto failed = file.open(base, O_CREAT | O_RDWR, 0600);
  fail_allocation = false;
  ASSERT_FALSE(failed.has_value());
  EXPECT_EQ(failed.error(), ENOMEM);
  EXPECT_EQ(file.descriptor(), -1);
  EXPECT_EQ(::access(name, F_OK), -1);
}

TEST(LlvmLibcHashFileTest, CloseConsumesOwnershipEvenOnError) {
  auto base = libc_make_test_file_path("hash-close");
  auto name = libc_make_test_file_path("hash-close.db");
  int reused;
  {
    db::HashFile file;
    ASSERT_TRUE(file.open(base, O_CREAT | O_TRUNC | O_RDWR, 0600).has_value());
    int original = file.descriptor();
    ASSERT_EQ(::close(original), 0);
    auto failed = file.close();
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error(), EBADF);
    EXPECT_EQ(file.descriptor(), -1);
    reused = ::open(name, O_RDONLY);
    ASSERT_GE(reused, 0);
    EXPECT_EQ(reused, original);
  }
  EXPECT_GE(::fcntl(reused, F_GETFD), 0);
  ::close(reused);
  ::unlink(name);
}

TEST(LlvmLibcHashFileTest, SynchronousFlagsAndCompletionErrors) {
  auto base = libc_make_test_file_path("hash-sync");
  auto name = libc_make_test_file_path("hash-sync.db");
  const int modes[] = {0, O_DSYNC, O_SYNC, O_DSYNC | O_SYNC};
  for (int mode : modes) {
    db::HashFile file;
    ASSERT_TRUE(file.open(base, O_CREAT | O_TRUNC | O_RDWR | mode, 0600)
                    .has_value());
    EXPECT_EQ(::fcntl(file.descriptor(), F_GETFL) & mode, mode);
    auto sync = file.synchronizer();
    if (mode == 0) {
      EXPECT_EQ(sync, nullptr);
    } else {
      ASSERT_NE(sync, nullptr);
      ASSERT_TRUE(sync(file.io().context).has_value());
      ASSERT_TRUE(file.close().has_value());
      auto failed = sync(file.io().context);
      ASSERT_FALSE(failed.has_value());
      EXPECT_EQ(failed.error(), EBADF);
    }
  }
  ::unlink(name);
}
