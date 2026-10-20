//===-- Public accounting API test cases ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TEST_SRC_UTMP_UTMP_TEST_H
#define LLVM_LIBC_TEST_SRC_UTMP_UTMP_TEST_H

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/stdio_macros.h"
#include "hdr/types/struct_flock.h"
#include "hdr/utmp_macros.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/fcntl.h"
#include "src/stdio/rename.h"
#include "src/stdlib/mkstemp.h"
#include "src/sys/stat/fchmod.h"
#include "src/unistd/close.h"
#include "src/unistd/ftruncate.h"
#include "src/unistd/lseek.h"
#include "src/unistd/read.h"
#include "src/unistd/unlink.h"
#include "src/unistd/write.h"
#include "test/UnitTest/Test.h"

#include <stddef.h>

template <typename Api> class UtmpTest : public LIBC_NAMESPACE::testing::Test {
protected:
  using Record = typename Api::Record;
  char path[4096]{};
  int fd = -1;

  static Record entry(short type, char id, int pid) {
    Record record{};
    record.ut_type = type;
    record.ut_pid = pid;
    record.ut_id[0] = id;
    record.ut_id[1] = '\0';
    record.ut_id[2] = 'X';
    record.ut_id[3] = 'Y';
    for (char &c : record.ut_line)
      c = id;
    return record;
  }

  void seed(const void *data, size_t size) {
    ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_SET), off_t(0));
    ASSERT_EQ(LIBC_NAMESPACE::ftruncate(fd, 0), 0);
    ASSERT_EQ(LIBC_NAMESPACE::write(fd, data, size), ssize_t(size));
    Api::set();
  }

  void expect_size(size_t size) {
    EXPECT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_END), off_t(size));
  }

public:
  void SetUp() override {
    auto full_path = libc_make_test_file_path("utmp-public.XXXXXX");
    const char *source = full_path;
    size_t i = 0;
    for (; source[i] && i < sizeof(path) - 1; ++i)
      path[i] = source[i];
    ASSERT_EQ(source[i], '\0');
    path[i] = '\0';
    fd = LIBC_NAMESPACE::mkstemp(path);
    ASSERT_GE(fd, 0);
    // Every test redirects the public engine BEFORE any read or write.
    ASSERT_EQ(Api::name(path), 0);
    libc_errno = 0;
  }

  void TearDown() override {
    Api::end();
    if (fd >= 0) {
      EXPECT_EQ(LIBC_NAMESPACE::close(fd), 0);
      EXPECT_EQ(LIBC_NAMESPACE::unlink(path), 0);
    }
  }

  void sequence() {
    Record records[] = {entry(BOOT_TIME, 'a', 11),
                        entry(USER_PROCESS, 'b', 22)};
    seed(records, sizeof(records));
    libc_errno = EDOM;
    auto *first = Api::get();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->ut_pid, 11);
    auto *second = Api::get();
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_EQ(second->ut_pid, 22);
    EXPECT_EQ(Api::get(), static_cast<Record *>(nullptr));
    EXPECT_EQ(int(libc_errno), EDOM);
    EXPECT_EQ(Api::get(), static_cast<Record *>(nullptr));
    Api::set();
    first = Api::get();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->ut_pid, 11);
    Api::end();
    Api::end();
    first = Api::get();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->ut_pid, 11);
  }

  void lookup() {
    Record records[] = {entry(BOOT_TIME, 'b', 10), entry(INIT_PROCESS, 'i', 20),
                        entry(LOGIN_PROCESS, 'l', 30),
                        entry(USER_PROCESS, 'u', 40),
                        entry(DEAD_PROCESS, 'd', 50)};
    seed(records, sizeof(records));
    Record key = entry(BOOT_TIME, 'x', 99);
    auto *found = Api::id(&key);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->ut_pid, 10);
    // ID lookup includes the last returned record, and all process types
    // match each other by four ID bytes, including bytes after a NUL.
    EXPECT_EQ(Api::id(&key), found);
    key = entry(DEAD_PROCESS, 'i', 99);
    found = Api::id(&key);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->ut_type, short(INIT_PROCESS));
    key.ut_id[3] = 'Z';
    EXPECT_EQ(Api::id(&key), static_cast<Record *>(nullptr));
    EXPECT_EQ(int(libc_errno), ESRCH);
    Api::set();
    key = entry(EMPTY, 'l', 99);
    found = Api::line(&key);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->ut_type, short(LOGIN_PROCESS));
    key = entry(EMPTY, 'u', 99);
    found = Api::line(&key);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->ut_type, short(USER_PROCESS));
    key = entry(EMPTY, 'd', 99);
    EXPECT_EQ(Api::line(&key), static_cast<Record *>(nullptr));
    EXPECT_EQ(int(libc_errno), ESRCH);
    Api::set();
    key = entry(EMPTY, 'i', 99);
    EXPECT_EQ(Api::line(&key), static_cast<Record *>(nullptr));
    EXPECT_EQ(int(libc_errno), ESRCH);
    key.ut_type = ACCOUNTING;
    EXPECT_EQ(Api::id(&key), static_cast<Record *>(nullptr));
    EXPECT_EQ(int(libc_errno), EINVAL);
  }

  void line_padding() {
    Record record = entry(USER_PROCESS, 't', 33);
    record.ut_line[1] = '\0';
    seed(&record, sizeof(record));
    Record key{};
    key.ut_line[0] = 't';
    auto *found = Api::line(&key);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->ut_pid, 33);
  }

  void replacement_and_append() {
    Record records[] = {entry(INIT_PROCESS, 'a', 10),
                        entry(USER_PROCESS, 'b', 20)};
    seed(records, sizeof(records));
    auto *record = Api::get();
    ASSERT_NE(record, nullptr);
    record->ut_type = LOGIN_PROCESS;
    record->ut_pid = 11;
    // Exercise write upgrade and an argument aliasing the static result.
    record = Api::put(record);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->ut_pid, 11);
    expect_size(sizeof(records));
    record = Api::get();
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->ut_pid, 20);
    Record appended = entry(USER_PROCESS, 'c', 30);
    ASSERT_NE(Api::put(&appended), nullptr);
    expect_size(3 * sizeof(Record));
    EXPECT_EQ(Api::get(), static_cast<Record *>(nullptr));
    Api::set();
    ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_SET), off_t(0));
    ASSERT_EQ(LIBC_NAMESPACE::read(fd, records, sizeof(records)),
              ssize_t(sizeof(records)));
    EXPECT_EQ(records[0].ut_pid, 11);
    EXPECT_EQ(records[0].ut_type, short(LOGIN_PROCESS));
    EXPECT_EQ(records[1].ut_pid, 20);
  }

  void read_only_then_upgrade() {
    Record record = entry(USER_PROCESS, 'a', 10);
    seed(&record, sizeof(record));
    Api::end();
    ASSERT_EQ(LIBC_NAMESPACE::fchmod(fd, 0400), 0);
    auto *found = Api::get();
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->ut_pid, 10);
    ASSERT_EQ(LIBC_NAMESPACE::fchmod(fd, 0600), 0);
    record.ut_pid = 20;
    ASSERT_NE(Api::put(&record), nullptr);
    expect_size(sizeof(record));
    Api::set();
    found = Api::get();
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->ut_pid, 20);
  }

  void replaced_path_write_upgrade() {
    Record records[] = {entry(USER_PROCESS, 'a', 10),
                        entry(USER_PROCESS, 'b', 20)};
    seed(records, sizeof(records));
    ASSERT_NE(Api::get(), nullptr);
    auto *second = Api::get();
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(second->ut_pid, 20);

    struct Replacement {
      char name[4096]{};
      int fd = -1;
      bool moved = false;
      ~Replacement() {
        if (fd >= 0) {
          LIBC_NAMESPACE::close(fd);
          if (!moved)
            LIBC_NAMESPACE::unlink(name);
        }
      }
    } replacement;
    auto full_path = libc_make_test_file_path("utmp-replacement.XXXXXX");
    const char *source = full_path;
    size_t i = 0;
    for (; i < sizeof(replacement.name) - 1 && source[i]; ++i)
      replacement.name[i] = source[i];
    ASSERT_EQ(source[i], '\0');
    replacement.fd = LIBC_NAMESPACE::mkstemp(replacement.name);
    ASSERT_GE(replacement.fd, 0);
    ASSERT_EQ(
        LIBC_NAMESPACE::write(replacement.fd, &records[1], sizeof(Record)),
        ssize_t(sizeof(Record)));
    ASSERT_EQ(LIBC_NAMESPACE::rename(replacement.name, path), 0);
    replacement.moved = true;

    // The read-only cursor still references the old two-record inode. The
    // upgrade must search the replacement from zero, not append at old EOF.
    Record update = records[1];
    update.ut_type = DEAD_PROCESS;
    update.ut_pid = 21;
    ASSERT_NE(Api::put(&update), nullptr);
    EXPECT_EQ(LIBC_NAMESPACE::lseek(replacement.fd, 0, SEEK_END),
              off_t(sizeof(Record)));
    ASSERT_EQ(LIBC_NAMESPACE::lseek(replacement.fd, 0, SEEK_SET), off_t(0));
    Record actual{};
    ASSERT_EQ(LIBC_NAMESPACE::read(replacement.fd, &actual, sizeof(actual)),
              ssize_t(sizeof(actual)));
    EXPECT_EQ(actual.ut_pid, 21);
    EXPECT_EQ(actual.ut_type, short(DEAD_PROCESS));
    EXPECT_EQ(actual.ut_id[0], 'b');
    EXPECT_EQ(Api::get(), static_cast<Record *>(nullptr));

    // The update must leave the old inode's contents unchanged.
    expect_size(sizeof(records));
    ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, sizeof(Record), SEEK_SET),
              off_t(sizeof(Record)));
    ASSERT_EQ(LIBC_NAMESPACE::read(fd, &actual, sizeof(actual)),
              ssize_t(sizeof(actual)));
    EXPECT_EQ(actual.ut_pid, 20);
    EXPECT_EQ(actual.ut_type, short(USER_PROCESS));
  }

  void corrupt_tail() {
    Record record = entry(USER_PROCESS, 'a', 10);
    seed(&record, sizeof(record));
    const char tail = '!';
    ASSERT_EQ(LIBC_NAMESPACE::write(fd, &tail, 1), ssize_t(1));
    ASSERT_NE(Api::get(), nullptr);
    libc_errno = 0;
    EXPECT_EQ(Api::get(), static_cast<Record *>(nullptr));
    EXPECT_EQ(int(libc_errno), EIO);
    libc_errno = 0;
    EXPECT_EQ(Api::get(), static_cast<Record *>(nullptr));
    EXPECT_EQ(int(libc_errno), EIO);
    Api::set();
    EXPECT_EQ(Api::put(&record), static_cast<Record *>(nullptr));
    EXPECT_EQ(int(libc_errno), EIO);
    Api::append(path, &record);
    EXPECT_EQ(int(libc_errno), EIO);
    expect_size(sizeof(record) + 1);
    ASSERT_EQ(LIBC_NAMESPACE::ftruncate(fd, sizeof(record)), 0);
    ASSERT_NE(Api::put(&record), nullptr);
    expect_size(sizeof(record));
  }

  void history() {
    Record record = entry(USER_PROCESS, 'a', 10);
    libc_errno = EDOM;
    Api::append(path, &record);
    EXPECT_EQ(int(libc_errno), EDOM);
    record.ut_type = DEAD_PROCESS;
    Api::append(path, &record);
    expect_size(2 * sizeof(record));
    Api::set();
    auto *found = Api::get();
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->ut_type, short(USER_PROCESS));
    found = Api::get();
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->ut_type, short(DEAD_PROCESS));
  }

  void missing_and_invalid() {
    EXPECT_EQ(Api::name(nullptr), -1);
    EXPECT_EQ(int(libc_errno), EINVAL);
    EXPECT_EQ(Api::name(""), -1);
    EXPECT_EQ(int(libc_errno), EINVAL);
    char too_long[4096];
    for (char &c : too_long)
      c = 'x';
    EXPECT_EQ(Api::name(too_long), -1);
    EXPECT_EQ(int(libc_errno), ENAMETOOLONG);
    EXPECT_EQ(Api::id(nullptr), static_cast<Record *>(nullptr));
    EXPECT_EQ(int(libc_errno), EINVAL);
    EXPECT_EQ(Api::line(nullptr), static_cast<Record *>(nullptr));
    EXPECT_EQ(int(libc_errno), EINVAL);
    EXPECT_EQ(Api::put(nullptr), static_cast<Record *>(nullptr));
    EXPECT_EQ(int(libc_errno), EINVAL);
    Record record = entry(USER_PROCESS, 'a', 10);
    ASSERT_NE(Api::put(&record), nullptr); // Invalid names retain the old path.
    Api::append(nullptr, &record);
    EXPECT_EQ(int(libc_errno), EINVAL);
    Api::append(path, nullptr);
    EXPECT_EQ(int(libc_errno), EINVAL);

    Api::end();
    ASSERT_EQ(LIBC_NAMESPACE::unlink(path), 0);
    EXPECT_EQ(Api::get(), static_cast<Record *>(nullptr));
    EXPECT_EQ(int(libc_errno), ENOENT);
    EXPECT_EQ(Api::put(&record), static_cast<Record *>(nullptr));
    EXPECT_EQ(int(libc_errno), ENOENT);
    Api::append(path, &record);
    EXPECT_EQ(int(libc_errno), ENOENT);
    // Neither write API may recreate an administratively absent database.
    EXPECT_EQ(LIBC_NAMESPACE::unlink(path), -1);
    EXPECT_EQ(int(libc_errno), ENOENT);
    ASSERT_EQ(LIBC_NAMESPACE::close(fd), 0);
    fd = -1;
  }

  void copied_path() {
    char copy[sizeof(path)];
    size_t i = 0;
    do {
      copy[i] = path[i];
    } while (path[i++]);
    ASSERT_EQ(Api::name(copy), 0);
    copy[0] = '\0';
    Record record = entry(USER_PROCESS, 'a', 10);
    ASSERT_NE(Api::put(&record), nullptr);
    expect_size(sizeof(record));
  }

  void file_locks() {
    Record record = entry(USER_PROCESS, 'a', 10);
    seed(&record, sizeof(record));
    // OFD locks conflict with POSIX locks even in the same process, allowing
    // deterministic contention tests without fork, sleeps or host files.
    struct flock lock{};
    lock.l_type = F_RDLCK;
    lock.l_whence = SEEK_SET;
    ASSERT_EQ(LIBC_NAMESPACE::fcntl(fd, F_OFD_SETLK, &lock), 0);
    ASSERT_NE(Api::get(), nullptr);
    EXPECT_EQ(Api::put(&record), static_cast<Record *>(nullptr));
    EXPECT_TRUE(int(libc_errno) == EAGAIN || int(libc_errno) == EACCES);
    libc_errno = 0;
    Api::append(path, &record);
    EXPECT_TRUE(int(libc_errno) == EAGAIN || int(libc_errno) == EACCES);
    expect_size(sizeof(record));
    lock.l_type = F_WRLCK;
    ASSERT_EQ(LIBC_NAMESPACE::fcntl(fd, F_OFD_SETLK, &lock), 0);
    Api::set();
    EXPECT_EQ(Api::get(), static_cast<Record *>(nullptr));
    EXPECT_TRUE(int(libc_errno) == EAGAIN || int(libc_errno) == EACCES);
    lock.l_type = F_UNLCK;
    ASSERT_EQ(LIBC_NAMESPACE::fcntl(fd, F_OFD_SETLK, &lock), 0);
    ASSERT_NE(Api::get(), nullptr);
    ASSERT_NE(Api::put(&record), nullptr);
    // A successful call must release its lock, including after an error.
    lock.l_type = F_WRLCK;
    ASSERT_EQ(LIBC_NAMESPACE::fcntl(fd, F_OFD_SETLK, &lock), 0);
    lock.l_type = F_UNLCK;
    ASSERT_EQ(LIBC_NAMESPACE::fcntl(fd, F_OFD_SETLK, &lock), 0);
    Api::append(path, &record);
    expect_size(2 * sizeof(record));
  }

  void raw_record() {
    static_assert(sizeof(Record) == 400 && alignof(Record) == 8);
    static_assert(offsetof(Record, ut_type) == 0);
    static_assert(offsetof(Record, ut_pid) == 4);
    static_assert(offsetof(Record, ut_line) == 8);
    static_assert(offsetof(Record, ut_id) == 40);
    static_assert(offsetof(Record, ut_user) == 44);
    static_assert(offsetof(Record, ut_host) == 76);
    static_assert(offsetof(Record, ut_exit) == 332);
    static_assert(offsetof(Record, ut_session) == 336);
    static_assert(offsetof(Record, ut_tv) == 344);
    static_assert(offsetof(Record, ut_addr_v6) == 360);
    static_assert(offsetof(Record, __glibc_reserved) == 376);
    unsigned char raw[400]{};
    // Construct native-endian bytes independently of the C record layout.
    auto integer = [&](size_t offset, unsigned bytes,
                       unsigned long long value) {
      for (unsigned i = 0; i < bytes; ++i) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        raw[offset + bytes - i - 1] =
            static_cast<unsigned char>(value >> (8 * i));
#else
        raw[offset + i] = static_cast<unsigned char>(value >> (8 * i));
#endif
      }
    };
    integer(0, 2, USER_PROCESS);
    integer(4, 4, 0x12345678);
    for (size_t i = 8; i < 332; ++i)
      raw[i] = 'A' + i % 26;
    integer(332, 2, 0x1234);
    integer(334, 2, 0x5678);
    integer(336, 8, 0x0102030405060708ULL);
    integer(344, 8, 0x1122334455667788ULL);
    integer(352, 8, 0x0123456789abcdefULL);
    for (unsigned i = 0; i < 4; ++i)
      integer(360 + i * 4, 4, 0x12345670 + i);
    for (unsigned i = 376; i < 400; ++i)
      raw[i] = 0xa5; // Include reserved bytes and tail padding in round-trip.
    seed(raw, sizeof(raw));
    auto *record = Api::get();
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->ut_pid, 0x12345678);
    EXPECT_EQ(record->ut_exit.e_termination, short(0x1234));
    EXPECT_EQ(record->ut_exit.e_exit, short(0x5678));
    EXPECT_EQ(record->ut_session, 0x0102030405060708L);
    EXPECT_EQ(record->ut_tv.tv_sec, static_cast<decltype(record->ut_tv.tv_sec)>(
                                        0x1122334455667788LL));
    EXPECT_EQ(
        record->ut_tv.tv_usec,
        static_cast<decltype(record->ut_tv.tv_usec)>(0x0123456789abcdefLL));
    for (unsigned i = 0; i < 4; ++i)
      EXPECT_EQ(record->ut_addr_v6[i], int(0x12345670 + i));
    ASSERT_NE(Api::put(record), nullptr);
    Api::append(path, record);
    unsigned char actual[800];
    ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_SET), off_t(0));
    ASSERT_EQ(LIBC_NAMESPACE::read(fd, actual, sizeof(actual)), ssize_t(800));
    for (unsigned i = 0; i < sizeof(actual); ++i)
      EXPECT_EQ(actual[i], raw[i % sizeof(raw)]);
  }
};

#endif // LLVM_LIBC_TEST_SRC_UTMP_UTMP_TEST_H
