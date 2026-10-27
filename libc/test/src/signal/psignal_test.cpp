//===-- Tests for psignal -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/stdio_macros.h"
#include "src/__support/File/file.h"
#include "src/__support/libc_errno.h"
#include "src/signal/psignal.h"
#include "src/stdio/stderr.h"
#include "src/string/strsignal.h"
#include "test/UnitTest/Test.h"
#include "test/src/locale/LocaleGuard.h"

using LIBC_NAMESPACE::File;
using LIBC_NAMESPACE::FileIOResult;

namespace {
constexpr File::Orientation ORIENTATIONS[] = {File::Orientation::UNORIENTED,
                                              File::Orientation::BYTE,
                                              File::Orientation::WIDE};

class Capture : public File {
  static FileIOResult write_output(File *base, const void *data, size_t count) {
    auto &file = *static_cast<Capture *>(base);
    if (++file.calls == file.fail_at)
      return {0, EIO};
    if (count > sizeof(file.bytes) - 1 - file.used)
      return {0, ENOSPC};
    for (size_t i = 0; i < count; ++i)
      file.bytes[file.used++] = static_cast<const char *>(data)[i];
    file.bytes[file.used] = '\0';
    return count;
  }

public:
  char bytes[256]{};
  size_t used = 0;
  unsigned calls = 0;
  unsigned fail_at = 0;
  explicit Capture(Orientation orientation)
      : File(write_output, nullptr, nullptr, nullptr, nullptr, 0, _IONBF, false,
             mode_flags("w")) {
    try_set_orientation_unlocked(orientation);
  }
};

class StderrGuard {
  ::FILE *saved = LIBC_NAMESPACE::stderr;

public:
  explicit StderrGuard(File &file) {
    LIBC_NAMESPACE::stderr = reinterpret_cast<::FILE *>(&file);
  }
  ~StderrGuard() { LIBC_NAMESPACE::stderr = saved; }
};
} // namespace

TEST(LlvmLibcPsignalTest, PrefixMessageAndOrientation) {
  LocaleGuard locale;
  const char *prefixes[] = {nullptr, "", "operation"};
  for (auto orientation : ORIENTATIONS) {
    for (const char *prefix : prefixes) {
      Capture file(orientation);
      StderrGuard guard(file);
      LIBC_NAMESPACE::libc_errno = EDOM;
      LIBC_NAMESPACE::psignal(SIGTERM, prefix);
      EXPECT_STREQ(file.bytes, prefix && *prefix ? "operation: Terminated\n"
                                                 : "Terminated\n");
      EXPECT_EQ(file.get_orientation(), orientation);
      EXPECT_FALSE(file.error());
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    }
  }
}

TEST(LlvmLibcPsignalTest, PreservesReturnedSignalMessage) {
  LocaleGuard locale;
  for (auto orientation : ORIENTATIONS) {
    Capture file(orientation);
    StderrGuard guard(file);
    const char *saved = LIBC_NAMESPACE::strsignal(-1234);
    ASSERT_STREQ(saved, "Unknown signal -1234");
    LIBC_NAMESPACE::libc_errno = ERANGE;
    LIBC_NAMESPACE::psignal(-5678, saved);
    EXPECT_STREQ(saved, "Unknown signal -1234");
    EXPECT_STREQ(file.bytes, "Unknown signal -1234: Unknown signal -5678\n");
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ERANGE);
    EXPECT_EQ(file.get_orientation(), orientation);
  }
}

TEST(LlvmLibcPsignalTest, RealTimeMessageMatchesStrsignal) {
  LocaleGuard locale;
  const int signals[] = {SIGRTMIN, SIGRTMAX};
  for (int signal : signals) {
    Capture file(File::Orientation::UNORIENTED);
    StderrGuard guard(file);
    const char *expected = LIBC_NAMESPACE::strsignal(signal);
    LIBC_NAMESPACE::psignal(signal, nullptr);
    size_t length = 0;
    while (expected[length]) {
      ASSERT_LT(length, file.used);
      EXPECT_EQ(file.bytes[length], expected[length]);
      ++length;
    }
    EXPECT_EQ(file.used, length + 1);
    EXPECT_EQ(file.bytes[length], '\n');
  }
}

TEST(LlvmLibcPsignalTest, OutputFailureSetsErrnoAndStreamError) {
  LocaleGuard locale;
  for (auto orientation : ORIENTATIONS) {
    for (unsigned failure = 1; failure <= 4; ++failure) {
      Capture file(orientation);
      StderrGuard guard(file);
      file.fail_at = failure;
      LIBC_NAMESPACE::libc_errno = 0;
      LIBC_NAMESPACE::psignal(SIGTERM, "prefix");
      EXPECT_EQ(file.calls, failure);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EIO);
      EXPECT_TRUE(file.error());
      EXPECT_EQ(file.get_orientation(), orientation);
    }
  }
}
