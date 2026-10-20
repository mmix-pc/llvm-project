//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "test/src/utmp/utmp_test.h"
#include "src/utmp/endutent.h"
#include "src/utmp/getutent.h"
#include "src/utmp/getutid.h"
#include "src/utmp/getutline.h"
#include "src/utmp/pututline.h"
#include "src/utmp/setutent.h"
#include "src/utmp/updwtmp.h"
#include "src/utmp/utmpname.h"

struct UtmpApi {
  using Record = struct utmp;
  static constexpr auto set = LIBC_NAMESPACE::setutent;
  static constexpr auto get = LIBC_NAMESPACE::getutent;
  static constexpr auto end = LIBC_NAMESPACE::endutent;
  static constexpr auto id = LIBC_NAMESPACE::getutid;
  static constexpr auto line = LIBC_NAMESPACE::getutline;
  static constexpr auto put = LIBC_NAMESPACE::pututline;
  static constexpr auto name = LIBC_NAMESPACE::utmpname;
  static constexpr auto append = LIBC_NAMESPACE::updwtmp;
};

using LlvmLibcUtmpTest = UtmpTest<UtmpApi>;

TEST_F(LlvmLibcUtmpTest, IterationRewindEndAndEof) { sequence(); }
TEST_F(LlvmLibcUtmpTest, IdAndBoundedLineLookup) { lookup(); }
TEST_F(LlvmLibcUtmpTest, LinePadding) { line_padding(); }
TEST_F(LlvmLibcUtmpTest, ReplaceAppendAndAliasedInput) {
  replacement_and_append();
}
TEST_F(LlvmLibcUtmpTest, ReadOnlyOpenAndWriteUpgrade) {
  read_only_then_upgrade();
}
TEST_F(LlvmLibcUtmpTest, ReplacedPathWriteUpgrade) {
  replaced_path_write_upgrade();
}
TEST_F(LlvmLibcUtmpTest, CorruptTail) { corrupt_tail(); }
TEST_F(LlvmLibcUtmpTest, HistoryAppend) { history(); }
TEST_F(LlvmLibcUtmpTest, MissingFilesAndInvalidArguments) {
  missing_and_invalid();
}
TEST_F(LlvmLibcUtmpTest, PathIsCopied) { copied_path(); }
TEST_F(LlvmLibcUtmpTest, RealFileLocks) { file_locks(); }
TEST_F(LlvmLibcUtmpTest, NativeRawRecord) { raw_record(); }
