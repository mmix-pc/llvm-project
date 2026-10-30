//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "hdr/func/realloc.h"
#include "src/__support/File/account_database.h"
#include "src/pwd/endpwent.h"
#include "src/pwd/getpwent.h"
#include "src/pwd/pwd_utils.h"
#include "src/pwd/setpwent.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE;

namespace {
File *next_stream;
unsigned open_calls;
bool requested_cloexec;
bool fail_malloc;
unsigned realloc_calls;
unsigned fail_realloc_at;
bool enumeration_detached();

constexpr char SHORT_RECORD[] = "root:x:0:0:Root:/root:/bin/sh\n";
constexpr char LONG_RECORD[] =
    "long:x:42:43:"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    ":/home/long:/bin/sh\n";

class TestFile : public File {
  static FileIOResult read(File *file, void *buffer, size_t size) {
    auto *self = static_cast<TestFile *>(file);
    if (self->consumed)
      return {0, EBADF};
    if (self->position == self->error_at) {
      self->error_at = static_cast<size_t>(-1);
      libc_errno = ENOTTY;
      if (self->data_on_error && size && self->position < self->length) {
        static_cast<char *>(buffer)[0] = self->data[self->position++];
        return {1, self->read_error};
      }
      return {0, self->read_error};
    }
    size_t copied = 0;
    while (copied < size && self->position < self->length &&
           self->position != self->error_at)
      static_cast<char *>(buffer)[copied++] = self->data[self->position++];
    return copied;
  }

  static ErrorOr<off_t> seek(File *file, off_t offset, int whence) {
    auto *self = static_cast<TestFile *>(file);
    if (self->consumed)
      return Error(EBADF);
    if (!self->seekable || (whence == SEEK_SET && self->seek_error)) {
      libc_errno = ENOTTY;
      return Error(self->seekable ? self->seek_error : ESPIPE);
    }
    off_t base = 0;
    if (whence == SEEK_CUR)
      base = off_t(self->position);
    else if (whence == SEEK_END)
      base = off_t(self->length);
    else if (whence != SEEK_SET)
      return Error(EINVAL);
    if (offset < -base || offset > off_t(self->length) - base)
      return Error(EINVAL);
    self->position = static_cast<size_t>(base + offset);
    return off_t(self->position);
  }

  static int close(File *file) {
    auto *self = static_cast<TestFile *>(file);
    ++self->close_calls;
    self->detached_on_close = enumeration_detached();
    libc_errno = ENOTTY;
    if (self->consumed)
      return EBADF;
    self->consumed = true;
    return self->close_error;
  }

public:
  const char *data = SHORT_RECORD;
  size_t length = sizeof(SHORT_RECORD) - 1;
  size_t position = 0;
  size_t error_at = static_cast<size_t>(-1);
  int read_error = EIO;
  bool data_on_error = false;
  bool seekable = true;
  int seek_error = 0;
  unsigned close_calls = 0;
  int close_error = 0;
  bool consumed = false;
  bool detached_on_close = false;

  TestFile()
      : File(nullptr, read, seek, close, nullptr, 0, _IONBF, false,
             File::mode_flags("r")) {}

  template <size_t N> void use(const char (&record)[N]) {
    data = record;
    length = N - 1;
  }
};
} // namespace

static void *passwd_test_malloc(size_t size) {
  return fail_malloc ? nullptr : ::malloc(size);
}

static void *passwd_test_realloc(void *pointer, size_t size) {
  if (++realloc_calls == fail_realloc_at)
    return nullptr;
  return ::realloc(pointer, size);
}

namespace LIBC_NAMESPACE_DECL {
ErrorOr<File *> passwd_test_openfile(const char *, const char *,
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

// Substitute only resource boundaries. CharVector, the enumeration reader,
// File operations, and the separately compiled public wrappers are production.
#define malloc passwd_test_malloc
#define realloc passwd_test_realloc
#define openfile passwd_test_openfile
#include "src/pwd/pwd_utils.cpp"
#undef openfile
#undef realloc
#undef malloc

namespace {
bool enumeration_detached() { return LIBC_NAMESPACE::passwd::database == nullptr; }

class LlvmLibcPwdFailureTest : public testing::Test {
protected:
  TestFile first;
  TestFile reopened;

  void SetUp() override {
    endpwent();
    // Only test isolation frees storage; public reset/close must retain it.
    ::free(LIBC_NAMESPACE::passwd::stream_buffer.buffer);
    LIBC_NAMESPACE::passwd::stream_buffer = {};
    next_stream = &first;
    open_calls = realloc_calls = fail_realloc_at = 0;
    requested_cloexec = fail_malloc = false;
    libc_errno = 0;
  }

  void TearDown() override {
    endpwent();
    next_stream = nullptr;
    fail_malloc = false;
    fail_realloc_at = 0;
    libc_errno = 0;
  }

  void checkLongResult() {
    auto *entry = getpwent();
    ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
    EXPECT_STREQ(entry->pw_name, "long");
    EXPECT_EQ(entry->pw_uid, uid_t(42));
    EXPECT_EQ(entry->pw_gid, gid_t(43));
    EXPECT_EQ(internal::string_length(entry->pw_gecos), size_t(192));
    EXPECT_STREQ(entry->pw_dir, "/home/long");
    EXPECT_STREQ(entry->pw_shell, "/bin/sh");
  }

  void checkClose(int error) {
    first.close_error = error;
    auto *entry = getpwent();
    ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
    char *name = entry->pw_name;
    char *shell = entry->pw_shell;
    EXPECT_TRUE(requested_cloexec);
    libc_errno = EDOM;
    endpwent();
    EXPECT_EQ(int(libc_errno), error ? error : EDOM);
    EXPECT_TRUE(first.consumed);
    EXPECT_TRUE(first.detached_on_close);
    EXPECT_EQ(first.close_calls, 1U);
    EXPECT_EQ(entry->pw_name, name);
    EXPECT_STREQ(name, "root");
    EXPECT_STREQ(shell, "/bin/sh");
    libc_errno = EDOM;
    endpwent();
    EXPECT_EQ(first.close_calls, 1U);
    EXPECT_EQ(int(libc_errno), EDOM);
    next_stream = &reopened;
    ASSERT_EQ(getpwent(), entry);
    EXPECT_STREQ(entry->pw_name, "root");
    EXPECT_EQ(open_calls, 2U);
    EXPECT_EQ(first.close_calls, 1U);
    endpwent();
    EXPECT_EQ(reopened.close_calls, 1U);
    EXPECT_TRUE(reopened.detached_on_close);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
};
} // namespace

TEST_F(LlvmLibcPwdFailureTest, ResultAllocationFailureRetriesRecord) {
  constexpr char record[] = "root:x:0:0:Root:/root:/bin/sh";
  first.use(record);
  fail_realloc_at = 1;
  EXPECT_EQ(getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOMEM);
  EXPECT_EQ(first.position, size_t(0));
  fail_realloc_at = 0;
  libc_errno = EDOM;
  auto *entry = getpwent();
  ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
  EXPECT_STREQ(entry->pw_name, "root");
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(open_calls, 1U);
}

TEST_F(LlvmLibcPwdFailureTest, LineAllocationFailureRetriesRecord) {
  first.use(LONG_RECORD);
  fail_malloc = true;
  EXPECT_EQ(getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOMEM);
  EXPECT_EQ(first.position, size_t(0));
  fail_malloc = false;
  checkLongResult();
}

TEST_F(LlvmLibcPwdFailureTest, LineGrowthFailureRetriesRecord) {
  first.use(LONG_RECORD);
  fail_realloc_at = 1;
  EXPECT_EQ(getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOMEM);
  EXPECT_EQ(first.position, size_t(0));
  fail_realloc_at = 0;
  checkLongResult();
}

TEST_F(LlvmLibcPwdFailureTest, ResultGrowthFailureRetainsPreviousStorage) {
  constexpr char records[] = "a:x:1:2:A:/:/s\n"
                             "oops\n"
                             "longer:x:3:4:Longer:/home/longer:/bin/sh\n";
  first.use(records);
  auto *entry = getpwent();
  ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
  char *name = entry->pw_name;
  size_t start = first.position + sizeof("oops\n") - 1;
  fail_realloc_at = realloc_calls + 1;
  EXPECT_EQ(getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOMEM);
  EXPECT_EQ(first.position, start);
  EXPECT_EQ(entry->pw_name, name);
  EXPECT_STREQ(name, "a");
  EXPECT_STREQ(entry->pw_shell, "/s");
  fail_realloc_at = 0;
  ASSERT_EQ(getpwent(), entry);
  EXPECT_STREQ(entry->pw_name, "longer");
}

TEST_F(LlvmLibcPwdFailureTest, IoErrorIsExplicitAndSeekableRecordCanRetry) {
  first.error_at = 5;
  first.read_error = ENOSPC;
  EXPECT_EQ(getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOSPC);
  EXPECT_EQ(first.position, size_t(0));
  libc_errno = EDOM;
  auto *entry = getpwent();
  ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
  EXPECT_STREQ(entry->pw_name, "root");
  EXPECT_EQ(int(libc_errno), EDOM);
}

TEST_F(LlvmLibcPwdFailureTest,
       NonseekableAllocationFailureDiscardsPartialLine) {
  constexpr char records[] =
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "forged:x:1:2:Forged:/:/bin/sh\n"
      "real:x:7:8:Real:/:/bin/sh\n";
  first.use(records);
  first.seekable = false;
  fail_malloc = true;
  EXPECT_EQ(getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOMEM);
  EXPECT_EQ(first.position, size_t(63));
  fail_malloc = false;
  setpwent();
  EXPECT_EQ(int(libc_errno), ESPIPE);
  libc_errno = EDOM;
  auto *entry = getpwent();
  ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
  EXPECT_STREQ(entry->pw_name, "real");
  EXPECT_EQ(int(libc_errno), EDOM);
}

TEST_F(LlvmLibcPwdFailureTest, FailedRollbackDiscardsPartialLine) {
  constexpr char records[] = "prefixforged:x:1:2:Forged:/:/bin/sh\n"
                             "real:x:7:8:Real:/:/bin/sh\n";
  first.use(records);
  first.error_at = 6;
  first.read_error = ENOSPC;
  first.seek_error = EIO;
  EXPECT_EQ(getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOSPC);
  EXPECT_EQ(first.position, size_t(6));
  auto *entry = getpwent();
  ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
  EXPECT_STREQ(entry->pw_name, "real");
}

TEST_F(LlvmLibcPwdFailureTest, NonseekableIoErrorBeforeRecordCanRetry) {
  first.seekable = false;
  first.error_at = 0;
  EXPECT_EQ(getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), EIO);
  libc_errno = EDOM;
  auto *entry = getpwent();
  ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
  EXPECT_STREQ(entry->pw_name, "root");
  EXPECT_EQ(int(libc_errno), EDOM);
}

TEST_F(LlvmLibcPwdFailureTest, NonseekableIoErrorsKeepDiscardingPartialRecord) {
  constexpr char records[] = "prefixforged:x:1:2:Forged:/:/bin/sh\n"
                             "real:x:7:8:Real:/:/bin/sh\n";
  first.use(records);
  first.seekable = false;
  first.error_at = 6;
  EXPECT_EQ(getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), EIO);
  first.error_at = 8;
  first.read_error = EINTR;
  EXPECT_EQ(getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), EINTR);
  libc_errno = EDOM;
  auto *entry = getpwent();
  ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
  EXPECT_STREQ(entry->pw_name, "real");
  EXPECT_EQ(int(libc_errno), EDOM);
}

TEST_F(LlvmLibcPwdFailureTest, NonseekableResultFailureConsumesOnlyThatRecord) {
  constexpr char records[] = "first:x:1:2:First:/:/bin/sh\n"
                             "next:x:7:8:Next:/:/bin/sh\n";
  first.use(records);
  first.seekable = false;
  fail_realloc_at = 1;
  EXPECT_EQ(getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOMEM);
  fail_realloc_at = 0;
  libc_errno = EDOM;
  auto *entry = getpwent();
  ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
  EXPECT_STREQ(entry->pw_name, "next");
  EXPECT_EQ(int(libc_errno), EDOM);
}

TEST_F(LlvmLibcPwdFailureTest, ErrorWithNewlineDoesNotDiscardNextRecord) {
  constexpr char records[] = "first:x:1:2:First:/:/bin/sh\n"
                             "next:x:7:8:Next:/:/bin/sh\n";
  first.use(records);
  first.seekable = false;
  first.error_at = sizeof("first:x:1:2:First:/:/bin/sh") - 1;
  first.data_on_error = true;
  EXPECT_EQ(getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), EIO);
  libc_errno = EDOM;
  auto *entry = getpwent();
  ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
  EXPECT_STREQ(entry->pw_name, "next");
  EXPECT_EQ(int(libc_errno), EDOM);
}

TEST_F(LlvmLibcPwdFailureTest, IoFailureRetainsPreviousResultAfterInvalidLine) {
  constexpr char records[] = "a:x:1:2:A:/:/s\n"
                             "oops\n"
                             "next:x:7:8:Next:/:/bin/sh\n";
  first.use(records);
  auto *entry = getpwent();
  ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
  char *name = entry->pw_name;
  first.error_at = first.position + sizeof("oops\n") - 1 + 3;
  EXPECT_EQ(getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), EIO);
  EXPECT_EQ(entry->pw_name, name);
  EXPECT_STREQ(name, "a");
  EXPECT_STREQ(entry->pw_shell, "/s");
  endpwent();
  EXPECT_STREQ(name, "a");
}

TEST_F(LlvmLibcPwdFailureTest, FailedResetKeepsBorrowedResultAndCursor) {
  first.seekable = false;
  auto *entry = getpwent();
  ASSERT_NE(entry, static_cast<struct passwd *>(nullptr));
  char *name = entry->pw_name;
  setpwent();
  EXPECT_EQ(int(libc_errno), ESPIPE);
  EXPECT_EQ(entry->pw_name, name);
  EXPECT_STREQ(name, "root");
  libc_errno = EDOM;
  EXPECT_EQ(getpwent(), static_cast<struct passwd *>(nullptr));
  EXPECT_EQ(int(libc_errno), EDOM);
  endpwent();
  EXPECT_STREQ(name, "root");
}

TEST_F(LlvmLibcPwdFailureTest, SuccessfulClosePreservesErrnoAndResult) {
  checkClose(0);
}

TEST_F(LlvmLibcPwdFailureTest, CloseIoErrorConsumesStream) { checkClose(EIO); }

TEST_F(LlvmLibcPwdFailureTest, InterruptedCloseIsNotRetried) {
  checkClose(EINTR);
}

TEST_F(LlvmLibcPwdFailureTest, CloseErrorIsNotCollapsed) { checkClose(ENOSPC); }
