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
#include "src/utmp/utmp_engine.h"
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

  static Record login_entry(short type, char id, int pid, const char *user) {
    Record record = entry(type, id, pid);
    record.ut_line[1] = '\0';
    for (size_t i = 0; i < sizeof(record.ut_user) && user[i]; ++i)
      record.ut_user[i] = user[i];
    return record;
  }

  static void clear(Record &record) {
    auto *bytes = reinterpret_cast<unsigned char *>(&record);
    for (size_t i = 0; i < sizeof(record); ++i)
      bytes[i] = 0;
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

  void cleared_line_progress() {
    Record records[] = {entry(LOGIN_PROCESS, 'a', 10),
                        entry(DEAD_PROCESS, 'a', 20),
                        entry(USER_PROCESS, 'b', 30),
                        entry(USER_PROCESS, 'a', 40)};
    records[3].ut_id[0] = 'c'; // Same line, distinct process ID key.
    seed(records, sizeof(records));
    const Record key = entry(EMPTY, 'a', 99);
    // Clearing works after enumeration as well as after a matching query.
    const bool starts[] = {false, true};
    for (bool enumerate_first : starts) {
      Api::set();
      auto *found = enumerate_first ? Api::get() : Api::line(&key);
      ASSERT_NE(found, nullptr);
      EXPECT_EQ(found->ut_pid, 10);
      if (!enumerate_first) {
        found = Api::line(&key);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->ut_pid, 10);
      }
      clear(*found);
      found = Api::line(&key);
      ASSERT_NE(found, nullptr);
      EXPECT_EQ(found->ut_pid, 40);
      EXPECT_EQ(found->ut_id[0], 'c');
      found = Api::line(&key);
      ASSERT_NE(found, nullptr);
      EXPECT_EQ(found->ut_pid, 40);
      clear(*found);
      for (int i = 0; i < 2; ++i) {
        libc_errno = EDOM;
        EXPECT_EQ(Api::line(&key), static_cast<Record *>(nullptr));
        EXPECT_EQ(int(libc_errno), ESRCH);
      }
    }
    Api::set();
    auto *found = Api::line(&key);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->ut_pid, 10);
  }

  void cleared_id_progress() {
    const short types[] = {USER_PROCESS, BOOT_TIME, OLD_TIME, NEW_TIME};
    for (short type : types) {
      Record records[] = {entry(type, 'a', 10), entry(USER_PROCESS, 'b', 20),
                          entry(type, 'a', 30)};
      if (type == USER_PROCESS) {
        records[0].ut_type = INIT_PROCESS;
        records[2].ut_type = DEAD_PROCESS;
      }
      // Time-event identity is the type, not the ID bytes.
      if (type != USER_PROCESS)
        records[2].ut_id[0] = 'c';
      records[0].ut_tv.tv_sec = 100;
      records[2].ut_tv.tv_sec = 300;
      seed(records, sizeof(records));
      const Record key = entry(type, 'a', 99);
      auto *found = Api::id(&key);
      ASSERT_NE(found, nullptr);
      EXPECT_EQ(found->ut_tv.tv_sec, decltype(found->ut_tv.tv_sec)(100));
      found = Api::id(&key);
      ASSERT_NE(found, nullptr);
      EXPECT_EQ(found->ut_tv.tv_sec, decltype(found->ut_tv.tv_sec)(100));
      clear(*found);
      found = Api::id(&key);
      ASSERT_NE(found, nullptr);
      EXPECT_EQ(found->ut_type, records[2].ut_type);
      EXPECT_EQ(found->ut_tv.tv_sec, decltype(found->ut_tv.tv_sec)(300));
      found = Api::id(&key);
      ASSERT_NE(found, nullptr);
      EXPECT_EQ(found->ut_tv.tv_sec, decltype(found->ut_tv.tv_sec)(300));
      clear(*found);
      for (int i = 0; i < 2; ++i) {
        libc_errno = EDOM;
        EXPECT_EQ(Api::id(&key), static_cast<Record *>(nullptr));
        EXPECT_EQ(int(libc_errno), ESRCH);
      }
      Api::set();
      found = Api::id(&key);
      ASSERT_NE(found, nullptr);
      EXPECT_EQ(found->ut_tv.tv_sec, decltype(found->ut_tv.tv_sec)(100));
    }
  }

  void login_preserves_cursor_and_record() {
    Record records[] = {login_entry(USER_PROCESS, 'a', 10, "alice"),
                        login_entry(USER_PROCESS, 'b', 20, "bob"),
                        login_entry(USER_PROCESS, 'c', 30, "carol")};
    seed(records, sizeof(records));
    auto *first = Api::get();
    ASSERT_NE(first, nullptr);
    const auto *bytes = reinterpret_cast<const unsigned char *>(first);
    unsigned char saved[sizeof(Record)];
    for (size_t i = 0; i < sizeof(saved); ++i)
      saved[i] = bytes[i];
    auto check_retained = [&]() {
      for (size_t i = 0; i < sizeof(saved); ++i)
        EXPECT_EQ(bytes[i], saved[i]);
    };

    char name[UT_NAMESIZE + 1];
    EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name("c", name, sizeof(name)),
              0);
    EXPECT_STREQ(name, "carol");
    check_retained();
    EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name("missing", name,
                                                    sizeof(name)),
              ENOENT);
    check_retained();
    EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name("c", name, 1), ERANGE);
    check_retained();
    auto *next = Api::get();
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->ut_pid, 20);
    next = Api::get();
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->ut_pid, 30);
    EXPECT_EQ(Api::get(), static_cast<Record *>(nullptr));

    // Private lookup starts at zero even when the public cursor is at EOF.
    EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name("a", name, sizeof(name)),
              0);
    EXPECT_STREQ(name, "alice");
    EXPECT_EQ(next->ut_pid, 30);
    EXPECT_EQ(Api::get(), static_cast<Record *>(nullptr));
  }

  void login_user_process_only() {
    const short types[] = {EMPTY,         RUN_LVL,      BOOT_TIME,
                           NEW_TIME,      OLD_TIME,     INIT_PROCESS,
                           LOGIN_PROCESS, DEAD_PROCESS, ACCOUNTING};
    char name[] = "unchanged";
    for (short type : types) {
      Record record = login_entry(type, 'a', 10, "ignored");
      seed(&record, sizeof(record));
      EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name("a", name,
                                                      sizeof(name)),
                ENOENT);
      EXPECT_STREQ(name, "unchanged");
    }

    Record records[] = {login_entry(LOGIN_PROCESS, 'a', 10, "login"),
                        login_entry(DEAD_PROCESS, 'a', 20, "dead"),
                        login_entry(USER_PROCESS, 'b', 30, "other"),
                        login_entry(USER_PROCESS, 'a', 40, "alice")};
    seed(records, sizeof(records));
    EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name("a", name, sizeof(name)),
              0);
    EXPECT_STREQ(name, "alice");

    Record empty = login_entry(USER_PROCESS, 'a', 10, "");
    seed(&empty, sizeof(empty));
    EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name("a", name, sizeof(name)),
              ENOENT);
    EXPECT_STREQ(name, "alice");
  }

  void login_name_bounds() {
    Record record = entry(USER_PROCESS, 't', 10);
    static_assert(sizeof(record.ut_user) == 32);
    for (size_t i = 0; i < sizeof(record.ut_user); ++i)
      record.ut_user[i] = 'a' + i % 26;
    seed(&record, sizeof(record));
    char line[sizeof(record.ut_line) + 1]{};
    for (size_t i = 0; i < sizeof(record.ut_line); ++i)
      line[i] = record.ut_line[i];
    char name[sizeof(record.ut_user) + 3];
    for (char &c : name)
      c = '!';
    EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name(line, name + 1, 33), 0);
    EXPECT_EQ(name[0], '!');
    for (size_t i = 0; i < sizeof(record.ut_user); ++i)
      EXPECT_EQ(name[i + 1], record.ut_user[i]);
    EXPECT_EQ(name[33], '\0');
    EXPECT_EQ(name[34], '!');

    const size_t sizes[] = {0, 1, 31, 32};
    for (size_t size : sizes) {
      for (char &c : name)
        c = '!';
      EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name(line, name + 1, size),
                ERANGE);
      for (char c : name)
        EXPECT_EQ(c, '!');
    }
    EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name(line, nullptr, 33),
              EINVAL);
    char too_long[sizeof(line) + 1];
    for (char &c : too_long)
      c = 't';
    too_long[sizeof(too_long) - 1] = '\0';
    EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name(too_long, name,
                                                    sizeof(name)),
              ENOENT);
    EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name("", name, sizeof(name)),
              ENOENT);
    for (char c : name)
      EXPECT_EQ(c, '!');

    // Bytes after an embedded username NUL are not part of the login name.
    record.ut_user[2] = '\0';
    seed(&record, sizeof(record));
    EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name(line, name + 1, 3), 0);
    EXPECT_STREQ(name + 1, "ab");
    EXPECT_EQ(name[0], '!');
    EXPECT_EQ(name[4], '!');
  }

  void login_empty_and_malformed_files() {
    char name[] = "unchanged";
    EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name("a", name, sizeof(name)),
              ENOENT);
    EXPECT_STREQ(name, "unchanged");
    Record record = login_entry(USER_PROCESS, 'a', 10, "alice");
    const size_t sizes[] = {1, sizeof(record) - 1};
    for (size_t size : sizes) {
      seed(&record, size);
      EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name("a", name,
                                                      sizeof(name)),
                EIO);
      EXPECT_STREQ(name, "unchanged");
    }
    seed(&record, sizeof(record));
    const char tail = '!';
    ASSERT_EQ(LIBC_NAMESPACE::write(fd, &tail, 1), ssize_t(1));
    // A matching complete record must not hide a malformed trailing record.
    EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name("a", name, sizeof(name)),
              EIO);
    EXPECT_STREQ(name, "unchanged");
  }

  void login_missing_path_preserves_descriptor() {
    Record records[] = {login_entry(USER_PROCESS, 'a', 10, "alice"),
                        login_entry(USER_PROCESS, 'b', 20, "bob")};
    seed(records, sizeof(records));
    auto *first = Api::get();
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first->ut_pid, 10);
    ASSERT_EQ(LIBC_NAMESPACE::unlink(path), 0);
    EXPECT_EQ(LIBC_NAMESPACE::close(fd), 0);
    fd = -1;

    // A cached public descriptor must not satisfy a private pathname lookup.
    char name[] = "unchanged";
    EXPECT_EQ(LIBC_NAMESPACE::utmp_backend::login_name("a", name, sizeof(name)),
              ENOENT);
    EXPECT_STREQ(name, "unchanged");
    EXPECT_EQ(first->ut_pid, 10);
    EXPECT_STREQ(first->ut_user, "alice");
    auto *next = Api::get();
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->ut_pid, 20);
    EXPECT_EQ(Api::get(), static_cast<Record *>(nullptr));
    EXPECT_EQ(LIBC_NAMESPACE::unlink(path), -1);
    EXPECT_EQ(int(libc_errno), ENOENT);
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

  void aliased_input_forward_search() {
    const bool modes[] = {false, true};
    for (bool writable_cursor : modes) {
      Api::end();
      Record records[] = {entry(USER_PROCESS, 'a', 10),
                          entry(USER_PROCESS, 'b', 20),
                          entry(USER_PROCESS, 'c', 30)};
      seed(records, sizeof(records));
      auto *record = writable_cursor ? Api::put(&records[0]) : Api::get();
      ASSERT_NE(record, nullptr);
      *record = login_entry(LOGIN_PROCESS, 'c', 31, "edited");
      // The modified cache matches the input, but its disk slot does not.
      // The implicit search must pass two records without clobbering input.
      record = Api::put(record);
      ASSERT_NE(record, nullptr);
      EXPECT_EQ(record->ut_pid, 31);
      EXPECT_EQ(record->ut_type, short(LOGIN_PROCESS));
      EXPECT_STREQ(record->ut_user, "edited");
      expect_size(sizeof(records));
      ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_SET), off_t(0));
      ASSERT_EQ(LIBC_NAMESPACE::read(fd, records, sizeof(records)),
                ssize_t(sizeof(records)));
      EXPECT_EQ(records[0].ut_pid, 10);
      EXPECT_EQ(records[0].ut_id[0], 'a');
      EXPECT_EQ(records[1].ut_pid, 20);
      EXPECT_EQ(records[1].ut_id[0], 'b');
      EXPECT_EQ(records[2].ut_pid, 31);
      EXPECT_EQ(records[2].ut_id[0], 'c');
      EXPECT_EQ(records[2].ut_type, short(LOGIN_PROCESS));
      EXPECT_STREQ(records[2].ut_user, "edited");
      EXPECT_EQ(Api::get(), static_cast<Record *>(nullptr));

      Api::set();
      record = Api::get();
      ASSERT_NE(record, nullptr);
      *record = login_entry(USER_PROCESS, 'd', 41, "appended");
      record = Api::put(record);
      ASSERT_NE(record, nullptr);
      EXPECT_EQ(record->ut_pid, 41);
      EXPECT_STREQ(record->ut_user, "appended");
      expect_size(4 * sizeof(Record));
      ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, sizeof(records), SEEK_SET),
                off_t(sizeof(records)));
      Record actual{};
      ASSERT_EQ(LIBC_NAMESPACE::read(fd, &actual, sizeof(actual)),
                ssize_t(sizeof(actual)));
      EXPECT_EQ(actual.ut_pid, 41);
      EXPECT_EQ(actual.ut_id[0], 'd');
      EXPECT_STREQ(actual.ut_user, "appended");
    }
  }

  void cleared_put_result_progress() {
    Record records[] = {entry(USER_PROCESS, 'a', 10),
                        entry(USER_PROCESS, 'a', 20)};
    seed(records, sizeof(records));
    auto *record = Api::put(&records[0]);
    ASSERT_NE(record, nullptr);
    clear(*record);
    const Record update = entry(USER_PROCESS, 'a', 30);
    record = Api::put(&update);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->ut_pid, 30);
    expect_size(sizeof(records));
    clear(*record);
    libc_errno = EDOM;
    EXPECT_EQ(Api::id(&update), static_cast<Record *>(nullptr));
    EXPECT_EQ(int(libc_errno), ESRCH);
    ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, 0, SEEK_SET), off_t(0));
    ASSERT_EQ(LIBC_NAMESPACE::read(fd, records, sizeof(records)),
              ssize_t(sizeof(records)));
    EXPECT_EQ(records[0].ut_pid, 10);
    EXPECT_EQ(records[1].ut_pid, 30);
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

  void cached_search_file_locks() {
    Record records[] = {entry(USER_PROCESS, 'a', 10),
                        entry(USER_PROCESS, 'a', 20)};
    seed(records, sizeof(records));
    const Record key = records[0];
    auto *found = Api::line(&key);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->ut_pid, 10);
    struct flock lock{};
    lock.l_whence = SEEK_SET;
    const bool states[] = {false, true};
    for (bool cleared : states) {
      if (cleared)
        clear(*found);
      lock.l_type = F_WRLCK;
      ASSERT_EQ(LIBC_NAMESPACE::fcntl(fd, F_OFD_SETLK, &lock), 0);
      EXPECT_EQ(Api::line(&key), static_cast<Record *>(nullptr));
      EXPECT_TRUE(int(libc_errno) == EAGAIN || int(libc_errno) == EACCES);
      EXPECT_EQ(Api::id(&key), static_cast<Record *>(nullptr));
      EXPECT_TRUE(int(libc_errno) == EAGAIN || int(libc_errno) == EACCES);
      lock.l_type = F_UNLCK;
      ASSERT_EQ(LIBC_NAMESPACE::fcntl(fd, F_OFD_SETLK, &lock), 0);
      found = Api::line(&key);
      ASSERT_NE(found, nullptr);
      EXPECT_EQ(found->ut_pid, cleared ? 20 : 10);
    }
    // Non-cleared repeats still revalidate the current on-disk record.
    records[1].ut_pid = 21;
    ASSERT_EQ(LIBC_NAMESPACE::lseek(fd, sizeof(Record), SEEK_SET),
              off_t(sizeof(Record)));
    ASSERT_EQ(LIBC_NAMESPACE::write(fd, &records[1], sizeof(Record)),
              ssize_t(sizeof(Record)));
    found = Api::id(&key);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->ut_pid, 21);
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
