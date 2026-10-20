//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/thread.h"
#include "src/utmp/endutent.h"
#include "src/utmp/getutent.h"
#include "src/utmp/pututline.h"
#include "src/utmp/setutent.h"
#include "src/utmp/updwtmp.h"
#include "src/utmp/utmpname.h"
#include "src/utmpx/endutxent.h"
#include "src/utmpx/getutxent.h"
#include "src/utmpx/getutxid.h"
#include "src/utmpx/getutxline.h"
#include "src/utmpx/pututxline.h"
#include "src/utmpx/setutxent.h"
#include "src/utmpx/updwtmpx.h"
#include "src/utmpx/utmpxname.h"
#include "test/src/utmp/utmp_test.h"

struct UtmpxApi {
  using Record = struct utmpx;
  static constexpr auto set = LIBC_NAMESPACE::setutxent;
  static constexpr auto get = LIBC_NAMESPACE::getutxent;
  static constexpr auto end = LIBC_NAMESPACE::endutxent;
  static constexpr auto id = LIBC_NAMESPACE::getutxid;
  static constexpr auto line = LIBC_NAMESPACE::getutxline;
  static constexpr auto put = LIBC_NAMESPACE::pututxline;
  static constexpr auto name = LIBC_NAMESPACE::utmpxname;
  static constexpr auto append = LIBC_NAMESPACE::updwtmpx;
};

using LlvmLibcUtmpxTest = UtmpTest<UtmpxApi>;

TEST_F(LlvmLibcUtmpxTest, IterationRewindEndAndEof) { sequence(); }
TEST_F(LlvmLibcUtmpxTest, IdAndBoundedLineLookup) { lookup(); }
TEST_F(LlvmLibcUtmpxTest, LinePadding) { line_padding(); }
TEST_F(LlvmLibcUtmpxTest, ReplaceAppendAndAliasedInput) {
  replacement_and_append();
}
TEST_F(LlvmLibcUtmpxTest, ReadOnlyOpenAndWriteUpgrade) {
  read_only_then_upgrade();
}
TEST_F(LlvmLibcUtmpxTest, ReplacedPathWriteUpgrade) {
  replaced_path_write_upgrade();
}
TEST_F(LlvmLibcUtmpxTest, CorruptTail) { corrupt_tail(); }
TEST_F(LlvmLibcUtmpxTest, HistoryAppend) { history(); }
TEST_F(LlvmLibcUtmpxTest, MissingFilesAndInvalidArguments) {
  missing_and_invalid();
}
TEST_F(LlvmLibcUtmpxTest, PathIsCopied) { copied_path(); }
TEST_F(LlvmLibcUtmpxTest, RealFileLocks) { file_locks(); }
TEST_F(LlvmLibcUtmpxTest, NativeRawRecord) { raw_record(); }

TEST_F(LlvmLibcUtmpxTest, SharedCursorAndPath) {
  Record records[] = {entry(INIT_PROCESS, 'a', 10),
                      entry(USER_PROCESS, 'b', 20)};
  seed(records, sizeof(records));
  auto *plain = LIBC_NAMESPACE::getutent();
  ASSERT_NE(plain, nullptr);
  EXPECT_EQ(plain->ut_pid, 10);
  auto *extended = LIBC_NAMESPACE::getutxent();
  ASSERT_NE(extended, nullptr);
  EXPECT_EQ(extended->ut_pid, 20);
  // Separate typed result storage, but one file position.
  EXPECT_EQ(plain->ut_pid, 10);
  EXPECT_EQ(LIBC_NAMESPACE::getutent(), static_cast<utmp *>(nullptr));
  LIBC_NAMESPACE::endutent();
  extended = LIBC_NAMESPACE::getutxent();
  ASSERT_NE(extended, nullptr);
  EXPECT_EQ(extended->ut_pid, 10);
  LIBC_NAMESPACE::setutent();
  extended = LIBC_NAMESPACE::getutxent();
  ASSERT_NE(extended, nullptr);
  EXPECT_EQ(extended->ut_pid, 10);
  // An x lookup followed by a plain write must replace the current record.
  plain->ut_type = LOGIN_PROCESS;
  plain->ut_pid = 11;
  ASSERT_NE(LIBC_NAMESPACE::pututline(plain), nullptr);
  expect_size(sizeof(records));
  LIBC_NAMESPACE::setutxent();
  extended = LIBC_NAMESPACE::getutxent();
  ASSERT_NE(extended, nullptr);
  EXPECT_EQ(extended->ut_pid, 11);

  // Selecting a path through the other family resets an already-open cursor.
  ASSERT_EQ(LIBC_NAMESPACE::utmpname(path), 0);
  extended = LIBC_NAMESPACE::getutxent();
  ASSERT_NE(extended, nullptr);
  EXPECT_EQ(extended->ut_pid, 11);
}

TEST_F(LlvmLibcUtmpxTest, ConcurrentHistoryWriters) {
  struct Context {
    const char *path;
    bool extended;
  } contexts[] = {{path, false}, {path, true}};
  auto worker = +[](void *argument) -> int {
    auto &context = *static_cast<Context *>(argument);
    utmp plain{};
    utmpx extended{};
    plain.ut_type = extended.ut_type = USER_PROCESS;
    for (int i = 1; i <= 32; ++i) {
      libc_errno = 0;
      if (context.extended) {
        extended.ut_pid = 100 + i;
        LIBC_NAMESPACE::updwtmpx(context.path, &extended);
      } else {
        plain.ut_pid = i;
        LIBC_NAMESPACE::updwtmp(context.path, &plain);
      }
      if (libc_errno)
        return int(libc_errno);
    }
    return 0;
  };
  LIBC_NAMESPACE::Thread first, second;
  ASSERT_EQ(first.run(worker, &contexts[0]), 0);
  int start = second.run(worker, &contexts[1]);
  int first_status = -1, second_status = -1;
  EXPECT_EQ(first.join(&first_status), 0);
  ASSERT_EQ(start, 0);
  EXPECT_EQ(second.join(&second_status), 0);
  EXPECT_EQ(first_status, 0);
  EXPECT_EQ(second_status, 0);
  expect_size(64 * sizeof(Record));
  LIBC_NAMESPACE::setutxent();
  bool seen[2][32]{};
  for (unsigned i = 0; i < 64; ++i) {
    auto *record = LIBC_NAMESPACE::getutxent();
    ASSERT_NE(record, nullptr);
    int group = record->ut_pid > 100 ? 1 : 0;
    int index = record->ut_pid - group * 100 - 1;
    ASSERT_GE(index, 0);
    ASSERT_LT(index, 32);
    EXPECT_FALSE(seen[group][index]);
    seen[group][index] = true;
  }
  EXPECT_EQ(LIBC_NAMESPACE::getutxent(), static_cast<utmpx *>(nullptr));
}
