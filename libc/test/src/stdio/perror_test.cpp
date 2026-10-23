//===-- Tests for diagnostic stream output --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/File/file.h"
#include "src/__support/StringUtil/error_to_string.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/perror.h"
#include "src/stdio/stderr.h"
#include "test/UnitTest/Test.h"
#include "test/src/locale/LocaleGuard.h"

using LIBC_NAMESPACE::File;
using LIBC_NAMESPACE::FileIOResult;
using LIBC_NAMESPACE::cpp::string_view;

namespace {
constexpr File::Orientation ORIENTATIONS[] = {File::Orientation::UNORIENTED,
                                              File::Orientation::BYTE,
                                              File::Orientation::WIDE};

class DiagnosticFile : public File {
  static FileIOResult write_output(File *base, const void *data, size_t count) {
    auto &file = *static_cast<DiagnosticFile *>(base);
    ++file.calls;
    file.changed_orientation |=
        file.get_orientation_unlocked() != file.expected_orientation;
    if (count > file.write_limit)
      count = file.write_limit;
    if (count > sizeof(file.output) - 1 - file.length)
      return {0, ENOSPC};
    for (size_t i = 0; i < count; ++i)
      file.output[file.length++] = static_cast<const char *>(data)[i];
    file.output[file.length] = '\0';
    return {count, file.write_error};
  }

public:
  char output[1024]{};
  size_t length = 0;
  size_t write_limit = sizeof(output);
  unsigned calls = 0;
  int write_error = 0;
  bool changed_orientation = false;
  Orientation expected_orientation;

  explicit DiagnosticFile(Orientation orientation)
      : File(write_output, nullptr, nullptr, nullptr, nullptr, 0, _IONBF, false,
             mode_flags("w")),
        expected_orientation(orientation) {
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

void append(char *buffer, size_t &size, string_view text) {
  for (char c : text)
    buffer[size++] = c;
  buffer[size] = '\0';
}
} // namespace

TEST(LlvmLibcPerrorTest, PrefixAndContentPreserveEveryOrientation) {
  LocaleGuard locale;
  const char *prefixes[] = {"operation", "", nullptr};
  const int errors[] = {0, EDOM, ERANGE, EILSEQ};
  for (auto orientation : ORIENTATIONS) {
    for (const char *prefix : prefixes) {
      for (int error : errors) {
        DiagnosticFile file(orientation);
        StderrGuard guard(file);
        char expected[1024]{};
        size_t size = 0;
        if (prefix && *prefix) {
          append(expected, size, prefix);
          append(expected, size, ": ");
        }
        append(expected, size, LIBC_NAMESPACE::get_error_string(error));
        append(expected, size, "\n");
        LIBC_NAMESPACE::libc_errno = error;
        LIBC_NAMESPACE::perror(prefix);
        EXPECT_STREQ(file.output, expected);
        EXPECT_EQ(file.length, size);
        EXPECT_FALSE(file.error());
        EXPECT_FALSE(file.changed_orientation);
        EXPECT_EQ(file.get_orientation(), orientation);
      }
    }
  }
}

TEST(LlvmLibcPerrorTest, WideOutputDecodesMultibytePrefix) {
  LocaleGuard locale;
  DiagnosticFile file(File::Orientation::WIDE);
  StderrGuard guard(file);
  char expected[1024]{};
  size_t size = 0;
  append(expected, size, "\xc2\xa2\xe2\x82\xac: ");
  append(expected, size, LIBC_NAMESPACE::get_error_string(EDOM));
  append(expected, size, "\n");
  LIBC_NAMESPACE::libc_errno = EDOM;
  LIBC_NAMESPACE::perror("\xc2\xa2\xe2\x82\xac");
  EXPECT_STREQ(file.output, expected);
  EXPECT_FALSE(file.error());
  EXPECT_FALSE(file.changed_orientation);
}

#ifdef LIBC_FULL_BUILD
TEST(LlvmLibcPerrorTest, WideOutputRetainsCapturedEncoding) {
  LocaleGuard utf8;
  DiagnosticFile file(File::Orientation::WIDE);
  StderrGuard guard(file);
  char expected[1024]{};
  size_t size = 0;
  append(expected, size, "\xc3\xa9: ");
  append(expected, size, LIBC_NAMESPACE::get_error_string(EDOM));
  append(expected, size, "\n");
  {
    LocaleGuard bytes(false);
    LIBC_NAMESPACE::libc_errno = EDOM;
    // The C-locale source byte is U+00E9; the stream still encodes UTF-8.
    LIBC_NAMESPACE::perror("\xe9");
  }
  EXPECT_STREQ(file.output, expected);
  EXPECT_FALSE(file.error());
  EXPECT_FALSE(file.changed_orientation);
}

TEST(LlvmLibcPerrorTest, WideOutputReportsUnrepresentableCharacter) {
  LocaleGuard bytes(false);
  DiagnosticFile file(File::Orientation::WIDE);
  StderrGuard guard(file);
  {
    LocaleGuard utf8;
    LIBC_NAMESPACE::libc_errno = EDOM;
    LIBC_NAMESPACE::perror("\xe2\x82\xac");
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EILSEQ);
  }
  EXPECT_EQ(file.length, size_t(0));
  EXPECT_TRUE(file.error());
  EXPECT_FALSE(file.changed_orientation);
}
#endif

TEST(LlvmLibcPerrorTest, WideOutputRejectsInvalidAndIncompleteInput) {
  LocaleGuard locale;
  const char *prefixes[] = {"x\xff", "x\xe2\x82"};
  for (const char *prefix : prefixes) {
    DiagnosticFile file(File::Orientation::WIDE);
    StderrGuard guard(file);
    LIBC_NAMESPACE::libc_errno = EDOM;
    LIBC_NAMESPACE::perror(prefix);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EILSEQ);
    EXPECT_STREQ(file.output, "x");
    EXPECT_TRUE(file.error());
    EXPECT_FALSE(file.changed_orientation);
  }
}

TEST(LlvmLibcPerrorTest, WriteErrorsAndShortWritesStopTheDiagnostic) {
  LocaleGuard locale;
  const int errors[] = {0, EAGAIN, EBADF};
  for (auto orientation : ORIENTATIONS) {
    for (int error : errors) {
      DiagnosticFile file(orientation);
      StderrGuard guard(file);
      file.write_limit = 0;
      file.write_error = error;
      LIBC_NAMESPACE::libc_errno = EDOM;
      LIBC_NAMESPACE::perror("prefix");
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error ? error : EIO);
      EXPECT_EQ(file.calls, 1U);
      EXPECT_EQ(file.length, size_t(0));
      EXPECT_TRUE(file.error());
      EXPECT_FALSE(file.changed_orientation);
      EXPECT_EQ(file.get_orientation(), orientation);
    }
  }
}

TEST(LlvmLibcPerrorTest, DiagnosticResultCountsConsumedInputBytes) {
  LocaleGuard locale;
  DiagnosticFile file(File::Orientation::WIDE);
  const char text[] = "\xc2\xa2x";
  auto result = file.write_diagnostic_unlocked(text, sizeof(text) - 1);
  EXPECT_EQ(result.value, sizeof(text) - 1);
  EXPECT_EQ(result.error, 0);
  EXPECT_STREQ(file.output, text);

  DiagnosticFile partial(File::Orientation::WIDE);
  partial.write_limit = 1;
  result = partial.write_diagnostic_unlocked(text, sizeof(text) - 1);
  EXPECT_EQ(result.value, size_t(0));
  EXPECT_EQ(result.error, EIO);
  EXPECT_EQ(partial.length, size_t(1));
  EXPECT_EQ(partial.calls, 1U);

  DiagnosticFile byte_error(File::Orientation::BYTE);
  byte_error.write_limit = 2;
  byte_error.write_error = EAGAIN;
  result = byte_error.write_diagnostic_unlocked("abcd", 4);
  EXPECT_EQ(result.value, size_t(2));
  EXPECT_EQ(result.error, EAGAIN);
  EXPECT_STREQ(byte_error.output, "ab");

  DiagnosticFile wide_error(File::Orientation::WIDE);
  wide_error.write_limit = 1;
  result = wide_error.write_diagnostic_unlocked("x\xc2\xa2", 3);
  EXPECT_EQ(result.value, size_t(1));
  EXPECT_EQ(result.error, EIO);
  EXPECT_EQ(wide_error.length, size_t(2));
  EXPECT_EQ(wide_error.calls, 2U);

  DiagnosticFile incomplete(File::Orientation::WIDE);
  result = incomplete.write_diagnostic_unlocked("x\xc2", 2);
  EXPECT_EQ(result.value, size_t(1));
  EXPECT_EQ(result.error, EILSEQ);

  DiagnosticFile empty(File::Orientation::UNORIENTED);
  result = empty.write_diagnostic_unlocked("", 0);
  EXPECT_EQ(result.value, size_t(0));
  EXPECT_EQ(result.error, 0);
  EXPECT_EQ(empty.calls, 0U);
  EXPECT_EQ(empty.get_orientation(), File::Orientation::UNORIENTED);
}
