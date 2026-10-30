//===-- Group enumeration close-failure tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/grp/endgrent.h"
#include "src/grp/getgrent.h"
#include "src/grp/stream.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE;

namespace {
File *next_stream;
unsigned open_calls;
bool requested_cloexec;

class CloseFile : public File {
  static constexpr char RECORD[] = "staff:x:7:alice,bob\n";
  size_t position = 0;

  static FileIOResult read(File *file, void *buffer, size_t size) {
    auto *self = static_cast<CloseFile *>(file);
    if (self->consumed)
      return {0, EBADF};
    size_t copied = 0;
    while (copied < size && self->position < sizeof(RECORD) - 1)
      static_cast<char *>(buffer)[copied++] = RECORD[self->position++];
    return copied;
  }

  static ErrorOr<off_t> seek(File *file, off_t offset, int whence) {
    auto *self = static_cast<CloseFile *>(file);
    if (self->consumed)
      return Error(EBADF);
    off_t base = 0;
    if (whence == SEEK_CUR)
      base = off_t(self->position);
    else if (whence == SEEK_END)
      base = off_t(sizeof(RECORD) - 1);
    else if (whence != SEEK_SET)
      return Error(EINVAL);
    if (offset < -base || offset > off_t(sizeof(RECORD) - 1) - base)
      return Error(EINVAL);
    self->position = static_cast<size_t>(base + offset);
    return off_t(self->position);
  }

  static int close(File *file) {
    auto *self = static_cast<CloseFile *>(file);
    ++self->close_calls;
    // The returned error, not an incidental errno change, is authoritative.
    libc_errno = ENOTTY;
    if (self->consumed)
      return EBADF;
    self->consumed = true;
    return self->close_error;
  }

public:
  unsigned close_calls = 0;
  int close_error = 0;
  bool consumed = false;

  CloseFile()
      : File(nullptr, read, seek, close, nullptr, 0, _IONBF, false,
             File::mode_flags("r")) {}
};
} // namespace

namespace LIBC_NAMESPACE_DECL {
ErrorOr<File *> group_test_openfile(const char *, const char *,
                                   bool close_on_exec) {
  ++open_calls;
  requested_cloexec = close_on_exec;
  if (!next_stream)
    return Error(ENOENT);
  File *file = next_stream;
  next_stream = nullptr;
  File::add_file(file);
  return file;
}
} // namespace LIBC_NAMESPACE_DECL

// Substitute only enumeration's open boundary; use the production reader,
// File::close and public wrappers without touching an application descriptor.
#define openfile group_test_openfile
#include "src/grp/stream.cpp"
#undef openfile

namespace {
class LlvmLibcGroupEndgrentFailureTest : public testing::Test {
protected:
  CloseFile first;
  CloseFile reopened;

  void SetUp() override {
    endgrent();
    next_stream = &first;
    open_calls = 0;
    requested_cloexec = false;
  }

  void TearDown() override {
    endgrent();
    next_stream = nullptr;
  }

  void checkClose(int error) {
    first.close_error = error;
    auto *entry = getgrent();
    ASSERT_NE(entry, static_cast<group *>(nullptr));
    EXPECT_STREQ(entry->gr_name, "staff");
    EXPECT_EQ(open_calls, 1U);
    EXPECT_TRUE(requested_cloexec);
    libc_errno = EDOM;
    endgrent();
    EXPECT_EQ(int(libc_errno), error ? error : EDOM);
    EXPECT_TRUE(first.consumed);
    EXPECT_EQ(first.close_calls, 1U);

    libc_errno = ENOSYS;
    endgrent();
    EXPECT_EQ(int(libc_errno), ENOSYS);
    EXPECT_EQ(first.close_calls, 1U);
    EXPECT_EQ(open_calls, 1U);

    next_stream = &reopened;
    entry = getgrent();
    ASSERT_NE(entry, static_cast<group *>(nullptr));
    EXPECT_STREQ(entry->gr_name, "staff");
    EXPECT_EQ(entry->gr_gid, gid_t(7));
    EXPECT_EQ(open_calls, 2U);
    EXPECT_EQ(first.close_calls, 1U);
    libc_errno = ERANGE;
    endgrent();
    EXPECT_EQ(int(libc_errno), ERANGE);
    EXPECT_TRUE(reopened.consumed);
    EXPECT_EQ(reopened.close_calls, 1U);
    EXPECT_EQ(first.close_calls, 1U);
  }
};
} // namespace

TEST_F(LlvmLibcGroupEndgrentFailureTest, SuccessPreservesErrno) {
  checkClose(0);
}

TEST_F(LlvmLibcGroupEndgrentFailureTest, IoErrorConsumesCursor) {
  checkClose(EIO);
}

TEST_F(LlvmLibcGroupEndgrentFailureTest, InterruptedCloseIsNotRetried) {
  checkClose(EINTR);
}

TEST_F(LlvmLibcGroupEndgrentFailureTest, NoSpaceErrorIsNotCollapsed) {
  checkClose(ENOSPC);
}
