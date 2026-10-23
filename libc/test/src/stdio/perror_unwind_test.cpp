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
#include "src/stdio/perror.h"
#include "src/stdio/stderr.h"
#include "test/UnitTest/Test.h"
#include <pthread.h>

using LIBC_NAMESPACE::File;
using LIBC_NAMESPACE::FileIOResult;

namespace LIBC_NAMESPACE_DECL {
FILE *stdin = nullptr;
FILE *stdout = nullptr;
FILE *stderr = nullptr;
} // namespace LIBC_NAMESPACE_DECL

namespace {
struct WriteFailure {};

class ThrowingDiagnostic : public File {
  static FileIOResult write_output(File *base, const void *, size_t count) {
    auto &self = *static_cast<ThrowingDiagnostic *>(base);
    self.changed_orientation |=
        self.get_orientation_unlocked() != self.expected_orientation;
    ++self.calls;
    if (self.fail)
      throw WriteFailure{};
    return count;
  }

public:
  bool fail = true, changed_orientation = false, acquired = false;
  unsigned calls = 0;
  Orientation expected_orientation;

  explicit ThrowingDiagnostic(Orientation orientation)
      : File(write_output, nullptr, nullptr, nullptr, nullptr, 0, _IONBF,
             false, mode_flags("w")),
        expected_orientation(orientation) {
    try_set_orientation_unlocked(orientation);
  }
};

class StderrGuard {
  FILE *saved = LIBC_NAMESPACE::stderr;

public:
  explicit StderrGuard(File &file) {
    LIBC_NAMESPACE::stderr = reinterpret_cast<FILE *>(&file);
  }
  ~StderrGuard() { LIBC_NAMESPACE::stderr = saved; }
};

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

TEST(LlvmLibcPerrorUnwindTest, ReleasesLockWithoutChangingOrientation) {
  const File::Orientation orientations[] = {File::Orientation::UNORIENTED,
                                          File::Orientation::BYTE,
                                          File::Orientation::WIDE};
  for (auto orientation : orientations) {
    ThrowingDiagnostic file(orientation);
    StderrGuard guard(file);
    LIBC_NAMESPACE::libc_errno = EDOM;
    bool caught = false;
    try {
      LIBC_NAMESPACE::perror("diagnostic");
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
    LIBC_NAMESPACE::perror(nullptr);
    EXPECT_FALSE(file.changed_orientation);
    EXPECT_FALSE(file.error());
  }
}
