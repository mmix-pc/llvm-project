//===-- Diagnostic stream lock cleanup during unwind ----------------------===//
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
#include "test/UnitTest/Test.h"
#include <pthread.h>

using LIBC_NAMESPACE::File;
using LIBC_NAMESPACE::FileIOResult;

namespace {
struct WriteFailure {};

class ThrowingDiagnostic : public File {
  static FileIOResult write_output(File *base, const void *data, size_t count) {
    auto &self = *static_cast<ThrowingDiagnostic *>(base);
    self.changed_orientation |=
        self.get_orientation_unlocked() != self.expected_orientation;
    ++self.calls;
    if (self.fail && self.calls > self.success_calls)
      throw WriteFailure{};
    if (count > self.limit)
      count = self.limit;
    for (size_t i = 0; i < count && self.used < sizeof(self.bytes); ++i)
      self.bytes[self.used++] = static_cast<const char *>(data)[i];
    return count;
  }

public:
  bool fail = true, changed_orientation = false, acquired = false;
  unsigned calls = 0;
  unsigned success_calls = 0;
  size_t limit = 1024, used = 0;
  char bytes[1024]{};
  Orientation expected_orientation;

  explicit ThrowingDiagnostic(Orientation orientation)
      : File(write_output, nullptr, nullptr, nullptr, nullptr, 0, _IONBF, false,
             mode_flags("w")),
        expected_orientation(orientation) {
    try_set_orientation_unlocked(orientation);
  }
};

void diagnostic(File &file, const char *text) {
  size_t size = 0;
  while (text[size])
    ++size;
  File::FileLock lock(&file);
  (void)file.write_diagnostic_unlocked(text, size);
}

void *acquire(void *arg) {
  auto &file = *static_cast<ThrowingDiagnostic *>(arg);
  // A try-lock makes a broken cleanup fail rather than hang. Another thread
  // is necessary because this stream lock is recursive.
  file.acquired = file.try_lock();
  if (file.acquired)
    file.unlock();
  return arg;
}
} // namespace

TEST(LlvmLibcDiagnosticUnwindTest, ReleasesLockWithoutChangingOrientation) {
  const File::Orientation orientations[] = {File::Orientation::UNORIENTED,
                                            File::Orientation::BYTE,
                                            File::Orientation::WIDE};
  for (auto orientation : orientations) {
    ThrowingDiagnostic file(orientation);
    LIBC_NAMESPACE::libc_errno = EDOM;
    bool caught = false;
    try {
      diagnostic(file, "diagnostic");
    } catch (WriteFailure) {
      caught = true;
    }
    ASSERT_TRUE(caught);
    EXPECT_EQ(file.calls, 1U);
    EXPECT_FALSE(file.changed_orientation);
    EXPECT_EQ(file.get_orientation(), orientation);
    pthread_t thread;
    ASSERT_EQ(::pthread_create(&thread, nullptr, acquire, &file), 0);
    ASSERT_EQ(::pthread_join(thread, nullptr), 0);
    ASSERT_TRUE(file.acquired);
    file.fail = false;
    diagnostic(file, "again");
    EXPECT_FALSE(file.changed_orientation);
    EXPECT_FALSE(file.error());
  }
}

TEST(LlvmLibcDiagnosticUnwindTest,
     BatchedShortWriteThenUnwindDoesNotReplayPrefix) {
  const size_t limits[] = {2, 3};
  for (size_t limit : limits) {
    ThrowingDiagnostic file(File::Orientation::WIDE);
    file.success_calls = 1;
    file.limit = limit;
    bool caught = false;
    try {
      diagnostic(file, "a\xc2\xa2z");
    } catch (WriteFailure) {
      caught = true;
    }
    ASSERT_TRUE(caught);
    EXPECT_EQ(file.calls, 2U);
    EXPECT_EQ(file.used, limit);
    EXPECT_EQ(file.bytes[0], 'a');
    EXPECT_EQ(file.bytes[1], '\xc2');
    if (limit == 3)
      EXPECT_EQ(file.bytes[2], '\xa2');
    pthread_t thread;
    ASSERT_EQ(::pthread_create(&thread, nullptr, acquire, &file), 0);
    ASSERT_EQ(::pthread_join(thread, nullptr), 0);
    ASSERT_TRUE(file.acquired);
    file.fail = false;
    EXPECT_EQ(file.flush(), 0);
    EXPECT_EQ(file.used, limit);
    EXPECT_EQ(file.write_diagnostic_unlocked("!", 1).value, size_t(1));
    EXPECT_EQ(file.used, limit + 1);
    EXPECT_EQ(file.bytes[limit], '!');
    EXPECT_FALSE(file.changed_orientation);
  }
}
