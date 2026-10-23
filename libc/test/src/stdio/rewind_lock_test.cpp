//===-- Tests for atomic rewind stream updates
//-----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "src/__support/File/file.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/rewind.h"
#include "test/UnitTest/Test.h"
#include <pthread.h>

namespace LIBC_NAMESPACE_DECL {

class RewindTestFile : public File {
  static FileIOResult read_empty(File *, void *, size_t) { return size_t(0); }

  static ErrorOr<off_t> reposition(File *base, off_t offset, int whence) {
    auto &self = *static_cast<RewindTestFile *>(base);
    self.correct_position = offset == 0 && whence == SEEK_SET;
    if (self.fail)
      return Error(EIO);
    return off_t(0);
  }

  static void *compete(void *arg) {
    auto &self = *static_cast<RewindTestFile *>(arg);
    self.acquired = self.try_lock();
    if (self.acquired) {
      self.set_error_unlocked();
      self.unlock();
    }
    return nullptr;
  }

public:
  bool fail = false, acquired = false, correct_position = false;
  int create_result = -1, join_result = -1;

  RewindTestFile()
      : File(nullptr, read_empty, reposition, nullptr, nullptr, 0, _IONBF,
             false, mode_flags("r")) {}

  void probe_lock() {
    pthread_t peer;
    create_result = ::pthread_create(&peer, nullptr, compete, this);
    if (create_result == 0)
      join_result = ::pthread_join(peer, nullptr);
  }

  ErrorOr<int> seek(off_t offset, int whence) {
    auto result = File::seek(offset, whence);
    // Interpose only at the wrapper boundary. The real File mutex and seek
    // run unchanged; the peer finishes its try-lock before clearerr can run.
    probe_lock();
    return result;
  }
};

} // namespace LIBC_NAMESPACE_DECL

// Exercise the production wrapper with a deterministic handoff after seek.
#define File RewindTestFile
#include "src/stdio/generic/rewind.cpp"
#undef File

TEST(LlvmLibcRewindLockTest, ExcludesIOUntilIndicatorsAreCleared) {
  const bool failures[] = {false, true};
  for (bool fail : failures) {
    LIBC_NAMESPACE::RewindTestFile file;
    file.fail = fail;
    char byte;
    ASSERT_EQ(file.read(&byte, 1).value, size_t(0));
    ASSERT_TRUE(file.iseof());
    file.set_error_unlocked();
    LIBC_NAMESPACE::libc_errno = 0;
    LIBC_NAMESPACE::rewind(reinterpret_cast<FILE *>(&file));
    EXPECT_EQ(file.create_result, 0);
    EXPECT_EQ(file.join_result, 0);
    EXPECT_TRUE(file.correct_position);
    EXPECT_FALSE(file.acquired);
    EXPECT_FALSE(file.error());
    EXPECT_FALSE(file.iseof());
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), fail ? EIO : 0);
    file.probe_lock();
    EXPECT_EQ(file.create_result, 0);
    EXPECT_EQ(file.join_result, 0);
    EXPECT_TRUE(file.acquired);
  }
}
