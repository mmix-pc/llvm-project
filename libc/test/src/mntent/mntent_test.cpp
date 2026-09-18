//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/mntent/endmntent.h"
#include "src/mntent/getmntent.h"
#include "src/mntent/getmntent_r.h"
#include "src/mntent/hasmntopt.h"
#include "src/mntent/mntent_utils.h"
#include "src/mntent/setmntent.h"
#include "src/stdio/fdopen.h"
#include "src/stdio/remove.h"
#include "src/unistd/close.h"
#include "src/unistd/dup.h"
#include "src/unistd/pipe.h"
#include "src/unistd/write.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE;
namespace {
template <size_t N> bool write_file(const char *path, const char (&text)[N]) {
  auto file = openfile(path, "w");
  if (!file)
    return false;
  auto written = file.value()->write(text, N - 1);
  int error = file.value()->close();
  return !written.has_error() && written.value == N - 1 && error == 0;
}
} // namespace

TEST(LlvmLibcMntentTest, RecordsEscapesAndEOF) {
  const char *path = libc_make_test_file_path("mount-records.test");
  const char records[] =
      "\n \t# comment\ninvalid\n"
      "bad /x fs rw x 0\nbad /x fs rw 0 99999999999999999\n"
      "bad /x fs rw 0 0 extra\nbad /x fs rw\0tail\n"
      "dev\\040name /mnt\\011tab f\\134s rw,tag=\\012 1 2 # comment\n"
      "next /next fs ro\nlast /last fs rw 3";
  ASSERT_TRUE(write_file(path, records));
  auto *stream = setmntent(path, "r");
  ASSERT_TRUE(stream != nullptr);
  mntent entry{}, other{};
  char buffer[256], second[256];
  libc_errno = EDOM;
  ASSERT_EQ(getmntent_r(stream, &entry, buffer, sizeof(buffer)), &entry);
  EXPECT_STREQ(entry.mnt_fsname, "dev name");
  EXPECT_STREQ(entry.mnt_dir, "/mnt\ttab");
  EXPECT_STREQ(entry.mnt_type, "f\\s");
  EXPECT_STREQ(entry.mnt_opts, "rw,tag=\n");
  EXPECT_EQ(entry.mnt_freq, 1);
  EXPECT_EQ(entry.mnt_passno, 2);
  EXPECT_EQ(int(libc_errno), EDOM);
  ASSERT_EQ(getmntent_r(stream, &other, second, sizeof(second)), &other);
  EXPECT_STREQ(other.mnt_fsname, "next");
  EXPECT_EQ(other.mnt_freq, 0);
  EXPECT_EQ(other.mnt_passno, 0);
  EXPECT_STREQ(entry.mnt_fsname, "dev name");
  ASSERT_EQ(getmntent_r(stream, &other, second, sizeof(second)), &other);
  EXPECT_EQ(other.mnt_freq, 3);
  EXPECT_EQ(other.mnt_passno, 0);
  EXPECT_EQ(getmntent_r(stream, &other, second, sizeof(second)),
            static_cast<mntent *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_TRUE(reinterpret_cast<File *>(stream)->iseof());
  EXPECT_FALSE(reinterpret_cast<File *>(stream)->error());
  EXPECT_EQ(endmntent(stream), 1);
  EXPECT_EQ(endmntent(nullptr), 1);
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcMntentTest, LimitsAndRecordRecovery) {
  const char *path = libc_make_test_file_path("mount-bounds.test");
  ASSERT_TRUE(write_file(path, "longdevice /long fs rw\nx / f r\n"));
  auto *stream = setmntent(path, "r");
  ASSERT_TRUE(stream != nullptr);
  mntent entry{};
  char storage[16];
  for (char &ch : storage)
    ch = 'Z';
  EXPECT_EQ(getmntent_r(stream, &entry, storage + 1, 8),
            static_cast<mntent *>(nullptr));
  EXPECT_EQ(int(libc_errno), ERANGE);
  EXPECT_EQ(storage[0], 'Z');
  EXPECT_EQ(storage[9], 'Z');
  ASSERT_EQ(getmntent_r(stream, &entry, storage + 1, 8), &entry);
  EXPECT_STREQ(entry.mnt_fsname, "x");
  EXPECT_EQ(storage[9], 'Z');
  EXPECT_EQ(endmntent(stream), 1);
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcMntentTest, DynamicGrowthAndRepeatedCalls) {
  const char *path = libc_make_test_file_path("mount-long.test");
  auto file = openfile(path, "w");
  ASSERT_TRUE(file.has_value());
  char name[8192];
  for (char &ch : name)
    ch = 'd';
  ASSERT_EQ(file.value()->write(name, sizeof(name)).value, sizeof(name));
  constexpr char tail[] = " / fs rw\nnext / fs ro\n";
  ASSERT_EQ(file.value()->write(tail, sizeof(tail) - 1).value,
            sizeof(tail) - 1);
  ASSERT_EQ(file.value()->close(), 0);
  auto *stream = setmntent(path, "r");
  ASSERT_TRUE(stream != nullptr);
  auto *first = getmntent(stream);
  ASSERT_TRUE(first != nullptr);
  EXPECT_EQ(first->mnt_fsname[8191], 'd');
  EXPECT_EQ(first->mnt_fsname[8192], '\0');
  auto *second = getmntent(stream);
  EXPECT_EQ(first, second);
  EXPECT_STREQ(second->mnt_fsname, "next");
  EXPECT_EQ(endmntent(stream), 1);
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcMntentTest, AllocationFailureDrainsRecord) {
  const char *path = libc_make_test_file_path("mount-allocation.test");
  ASSERT_TRUE(write_file(path, "dev / fs rw\nnext / fs ro\n"));
  auto *stream = setmntent(path, "r");
  ASSERT_TRUE(stream != nullptr);
  char *buffer = nullptr;
  size_t size = 0;
  mntent entry;
  EXPECT_EQ(internal::read_mount_entry(
                reinterpret_cast<File *>(stream), &entry, buffer, size,
                [](void *, size_t) -> void * { return nullptr; }),
            static_cast<mntent *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOMEM);
  EXPECT_EQ(buffer, static_cast<char *>(nullptr));
  char fixed[128];
  ASSERT_EQ(getmntent_r(stream, &entry, fixed, sizeof(fixed)), &entry);
  EXPECT_STREQ(entry.mnt_fsname, "next");
  EXPECT_EQ(endmntent(stream), 1);
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcMntentTest, OptionsAndUnknownEscapes) {
  char options[] = "errors=remount-ro,ro,rwother,foo=value,foo2";
  mntent entry{};
  entry.mnt_opts = options;
  EXPECT_EQ(hasmntopt(&entry, "errors"), options);
  EXPECT_EQ(hasmntopt(&entry, "errors=remount-ro"), options);
  EXPECT_TRUE(hasmntopt(&entry, "ro") != nullptr);
  EXPECT_TRUE(hasmntopt(&entry, "foo") != nullptr);
  EXPECT_EQ(hasmntopt(&entry, "rw"), static_cast<char *>(nullptr));
  EXPECT_EQ(hasmntopt(&entry, "remount-ro"), static_cast<char *>(nullptr));
  EXPECT_EQ(hasmntopt(&entry, "foo=val"), static_cast<char *>(nullptr));
  EXPECT_EQ(hasmntopt(&entry, ""), static_cast<char *>(nullptr));
  EXPECT_EQ(hasmntopt(nullptr, "rw"), static_cast<char *>(nullptr));
  const char *path = libc_make_test_file_path("mount-escapes.test");
  ASSERT_TRUE(write_file(path, "d\\777 /slash\\ x rw\n"));
  auto *stream = setmntent(path, "r");
  ASSERT_TRUE(stream != nullptr);
  auto *value = getmntent(stream);
  ASSERT_TRUE(value != nullptr);
  EXPECT_STREQ(value->mnt_fsname, "d\\777");
  EXPECT_STREQ(value->mnt_dir, "/slash\\");
  EXPECT_EQ(endmntent(stream), 1);
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
}

TEST(LlvmLibcMntentTest, ErrorsAndDescriptorCleanup) {
  EXPECT_EQ(setmntent("/nonexistent/mount-table", "r"),
            static_cast<::FILE *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOENT);
  EXPECT_EQ(setmntent(".", "invalid"), static_cast<::FILE *>(nullptr));
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(getmntent(nullptr), static_cast<mntent *>(nullptr));
  EXPECT_EQ(int(libc_errno), EINVAL);
  int before = dup(1);
  ASSERT_GE(before, 0);
  ASSERT_EQ(close(before), 0);
  for (unsigned i = 0; i < 64; ++i) {
    auto *stream = setmntent(".", "r");
    ASSERT_TRUE(stream != nullptr);
    mntent entry;
    char buffer[16];
    EXPECT_EQ(getmntent_r(stream, &entry, buffer, 0),
              static_cast<mntent *>(nullptr));
    EXPECT_EQ(int(libc_errno), EINVAL);
    EXPECT_EQ(getmntent_r(stream, &entry, buffer, sizeof(buffer)),
              static_cast<mntent *>(nullptr));
    EXPECT_EQ(int(libc_errno), EISDIR);
    EXPECT_TRUE(reinterpret_cast<File *>(stream)->error());
    EXPECT_EQ(endmntent(stream), 1);
  }
  int after = dup(1);
  EXPECT_EQ(after, before);
  ASSERT_EQ(close(after), 0);
}

TEST(LlvmLibcMntentTest, NonseekableStream) {
  int fds[2];
  ASSERT_EQ(LIBC_NAMESPACE::pipe(fds), 0);
  char line[2048];
  for (char &ch : line)
    ch = 'd';
  const char tail[] = " / fs rw\n";
  ASSERT_EQ(LIBC_NAMESPACE::write(fds[1], line, sizeof(line)),
            ssize_t(sizeof(line)));
  ASSERT_EQ(LIBC_NAMESPACE::write(fds[1], tail, sizeof(tail) - 1),
            ssize_t(sizeof(tail) - 1));
  ASSERT_EQ(LIBC_NAMESPACE::close(fds[1]), 0);
  auto *stream = LIBC_NAMESPACE::fdopen(fds[0], "r");
  ASSERT_TRUE(stream != nullptr);
  auto *entry = getmntent(stream);
  ASSERT_TRUE(entry != nullptr);
  EXPECT_EQ(entry->mnt_fsname[2047], 'd');
  EXPECT_EQ(entry->mnt_fsname[2048], '\0');
  EXPECT_EQ(endmntent(stream), 1);
  EXPECT_STREQ(entry->mnt_dir, "/");
}
