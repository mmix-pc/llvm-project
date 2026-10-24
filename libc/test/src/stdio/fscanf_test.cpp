//===-- Unittests for fscanf ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/fscanf.h"

#ifndef LIBC_COPT_STDIO_USE_SYSTEM_FILE
#include "src/__support/File/file.h"
#include "src/stdio/fclose.h"
#include "src/stdio/feof.h"
#include "src/stdio/ferror.h"
#include "src/stdio/fopen.h"
#include "src/stdio/fwrite.h"
#include "test/src/locale/LocaleGuard.h"
#endif // LIBC_COPT_STDIO_USE_SYSTEM_FILE

#include "test/UnitTest/Test.h"

namespace scanf_test {
#ifndef LIBC_COPT_STDIO_USE_SYSTEM_FILE
using LIBC_NAMESPACE::fclose;
using LIBC_NAMESPACE::feof;
using LIBC_NAMESPACE::ferror;
using LIBC_NAMESPACE::fopen;
using LIBC_NAMESPACE::fwrite;
#else  // defined(LIBC_COPT_STDIO_USE_SYSTEM_FILE)
using ::fclose;
using ::feof;
using ::ferror;
using ::fopen;
using ::fwrite;
#endif // LIBC_COPT_STDIO_USE_SYSTEM_FILE
} // namespace scanf_test

#ifndef LIBC_COPT_STDIO_USE_SYSTEM_FILE
namespace {
class CountingInput : public LIBC_NAMESPACE::File {
  LIBC_NAMESPACE::cpp::string_view input;
  int end_error;
  size_t offset = 0;

  static LIBC_NAMESPACE::FileIOResult read_input(File *base, void *out,
                                                 size_t) {
    auto &self = *static_cast<CountingInput *>(base);
    ++self.read_calls;
    if (self.offset == self.input.size())
      return {0, self.end_error};
    *static_cast<char *>(out) = self.input[self.offset++];
    return 1;
  }

public:
  size_t read_calls = 0;

  CountingInput(LIBC_NAMESPACE::cpp::string_view input, int end_error)
      : File(nullptr, read_input, nullptr, nullptr, nullptr, 0, _IONBF, false,
             mode_flags("r")),
        input(input), end_error(end_error) {}
};
} // namespace

TEST(LlvmLibcFScanfTest, IntegerWidthDoesNotReadPastBoundary) {
  struct {
    const char *input;
    const char *format;
    int matches;
    int value;
    bool is_unsigned;
    bool suppressed;
  } cases[] = {{"7", "%1d%n", 1, 7, false, false},
               {"-7", "%2d%n", 1, -7, false, false},
               {"+7", "%2i%n", 1, 7, false, false},
               {"0", "%1i%n", 1, 0, false, false},
               {"-0", "%2i%n", 1, 0, false, false},
               {"07", "%2i%n", 1, 7, false, false},
               {"0x7", "%3i%n", 1, 7, false, false},
               {"-0X7", "%4i%n", 1, -7, false, false},
               {"7", "%1u%n", 1, 7, true, false},
               {"07", "%2o%n", 1, 7, true, false},
               {"0", "%1x%n", 1, 0, true, false},
               {"f", "%1x%n", 1, 15, true, false},
               {"+0XF", "%4X%n", 1, 15, true, false},
               {"-", "%1d%n", 0, 37, false, false},
               {"+", "%1i%n", 0, 37, false, false},
               {"-", "%1u%n", 0, 37, true, false},
               {"+", "%1o%n", 0, 37, true, false},
               {"-", "%1x%n", 0, 37, true, false},
               {"+", "%1X%n", 0, 37, true, false},
               {"0x", "%2i%n", 0, 37, false, false},
               {"-0X", "%3i%n", 0, 37, false, false},
               {"0x", "%2x%n", 0, 37, true, false},
               {"+0X", "%3X%n", 0, 37, true, false},
               {"7", "%*1d%n", 1, 37, false, true},
               {"-0X7", "%*4i%n", 1, 37, false, true},
               {"0XF", "%*3X%n", 1, 37, false, true},
               {"-", "%*1d%n", 0, 37, false, true},
               {"0x", "%*2i%n", 0, 37, false, true}};
  const int end_errors[] = {0, EAGAIN};
  for (int end_error : end_errors) {
    for (const auto &test : cases) {
      LIBC_NAMESPACE::cpp::string_view input(test.input);
      // EAGAIN models an empty nonblocking pipe with its writer still open.
      // One byte per backend call also detects a read that would block.
      CountingInput source(input, end_error);
      auto *file = reinterpret_cast<FILE *>(&source);
      int count = -1;
      LIBC_NAMESPACE::libc_errno = 0;
      if (test.suppressed) {
        EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, test.format, &count), 0);
      } else if (test.is_unsigned) {
        unsigned value = 37;
        EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, test.format, &value, &count),
                  test.matches);
        EXPECT_EQ(value, static_cast<unsigned>(test.value));
      } else {
        int value = 37;
        EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, test.format, &value, &count),
                  test.matches);
        EXPECT_EQ(value, test.value);
      }
      EXPECT_EQ(count, test.matches ? int(input.size()) : -1);
      EXPECT_EQ(source.read_calls, input.size());
      EXPECT_EQ(scanf_test::feof(file), 0);
      EXPECT_EQ(scanf_test::ferror(file), 0);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), 0);

      // Only an explicit subsequent read may observe EOF or EAGAIN.
      char next = '?';
      EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%c", &next), EOF);
      EXPECT_EQ(next, '?');
      EXPECT_EQ(source.read_calls, input.size() + 1);
      EXPECT_EQ(scanf_test::feof(file) != 0, end_error == 0);
      EXPECT_EQ(scanf_test::ferror(file) != 0, end_error != 0);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), end_error);
    }
  }
}

TEST(LlvmLibcFScanfTest, IntegerLookaheadWithinWidth) {
  struct {
    const char *input;
    const char *format;
    int result;
    int value;
    int count;
    char next;
    bool eof;
  } cases[] = {{"7!", "%2d%n", 1, 7, 1, '!', false},
               {"09", "%2i%n", 1, 0, 1, '9', false},
               {"0xG", "%4i%n", 0, 37, -1, 'G', false},
               {"+!", "%i%n", 0, 37, -1, '!', false},
               {"0", "%2i%n", 1, 0, 1, '?', true},
               {"7", "%d%n", 1, 7, 1, '?', true},
               {"-", "%2d%n", 0, 37, -1, '?', true},
               {"0x", "%3i%n", 0, 37, -1, '?', true},
               {"", "%1i%n", EOF, 37, -1, '?', true}};
  for (const auto &test : cases) {
    LIBC_NAMESPACE::cpp::string_view input(test.input);
    CountingInput source(input, 0);
    auto *file = reinterpret_cast<FILE *>(&source);
    int value = 37;
    int count = -1;
    EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, test.format, &value, &count),
              test.result);
    EXPECT_EQ(value, test.value);
    EXPECT_EQ(count, test.count);
    EXPECT_EQ(scanf_test::feof(file) != 0, test.eof);
    EXPECT_EQ(scanf_test::ferror(file), 0);
    EXPECT_EQ(source.read_calls, input.size() + size_t(test.eof));
    if (!test.eof) {
      char next = '?';
      EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%c", &next), 1);
      EXPECT_EQ(next, test.next);
      // The unmatched byte was already read and must come from pushback.
      EXPECT_EQ(source.read_calls, input.size());
    }
  }
}

TEST(LlvmLibcFScanfTest, ReadErrorPreservesErrno) {
  auto path = libc_make_test_file_path("scanf_read_error.test");
  ::FILE *file = scanf_test::fopen(path, "w");
  ASSERT_NE(file, nullptr);
  int value = 19;
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%d", &value), EOF);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
  EXPECT_NE(scanf_test::ferror(file), 0);
  EXPECT_EQ(value, 19);
  EXPECT_EQ(scanf_test::fclose(file), 0);
}

TEST(LlvmLibcFScanfTest, EncodingErrorSetsStreamIndicator) {
  LocaleGuard locale;
  auto path = libc_make_test_file_path("scanf_encoding_error.test");
  ::FILE *file = scanf_test::fopen(path, "w");
  ASSERT_NE(file, nullptr);
  ASSERT_EQ(scanf_test::fwrite("\xff", 1, 1, file), size_t(1));
  ASSERT_EQ(scanf_test::fclose(file), 0);
  file = scanf_test::fopen(path, "r");
  ASSERT_NE(file, nullptr);
  wchar_t value = L'!';
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%lc", &value), EOF);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EILSEQ);
  EXPECT_NE(scanf_test::ferror(file), 0);
  EXPECT_EQ(value, L'!');
  EXPECT_EQ(scanf_test::fclose(file), 0);
}

TEST(LlvmLibcFScanfTest, ReadErrorInsideMultibyteCharacter) {
  LocaleGuard locale;
  class FailedInput : public LIBC_NAMESPACE::File {
    bool supplied = false;
    static LIBC_NAMESPACE::FileIOResult read_input(File *base, void *out,
                                                   size_t) {
      auto &self = *static_cast<FailedInput *>(base);
      if (self.supplied)
        return {0, EIO};
      self.supplied = true;
      *static_cast<unsigned char *>(out) = 0xc2;
      return 1;
    }

  public:
    FailedInput()
        : File(nullptr, read_input, nullptr, nullptr, nullptr, 0, _IONBF, false,
               mode_flags("r")) {}
  } file;
  wchar_t value = L'!';
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(
      LIBC_NAMESPACE::fscanf(reinterpret_cast<FILE *>(&file), "%lc", &value),
      EOF);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EIO);
  EXPECT_TRUE(file.error());
  EXPECT_EQ(value, L'!');
}
#endif

TEST(LlvmLibcFScanfTest,
     IntegerMatchingFailurePreservesDestinationAndNextByte) {
  struct {
    const char *input;
    const char *format;
    char next;
  } cases[] = {{"-1", "%1d%n", '1'},   {"+1", "%1d%n", '1'},
               {"-1", "%1i%n", '1'},   {"+1", "%1i%n", '1'},
               {"!", "%i%n", '!'},     {"+!", "%i%n", '!'},
               {"-!", "%i%n", '!'},    {"0x1", "%2i%n", '1'},
               {"-0X1", "%3i%n", '1'}, {"0xG", "%i%n", 'G'}};
  auto path = libc_make_test_file_path("scanf_integer_matching_failure.test");
  for (const auto &test : cases) {
    LIBC_NAMESPACE::cpp::string_view input(test.input);
    ::FILE *file = scanf_test::fopen(path, "w");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(scanf_test::fwrite(input.data(), 1, input.size(), file),
              input.size());
    ASSERT_EQ(scanf_test::fclose(file), 0);
    file = scanf_test::fopen(path, "r");
    ASSERT_NE(file, nullptr);
    int value = 37;
    int count = -1;
    EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, test.format, &value, &count), 0);
    EXPECT_EQ(value, 37);
    EXPECT_EQ(count, -1);
    EXPECT_EQ(scanf_test::feof(file), 0);
    EXPECT_EQ(scanf_test::ferror(file), 0);
    char next = '?';
    EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%c", &next), 1);
    EXPECT_EQ(next, test.next);
    EXPECT_EQ(scanf_test::fclose(file), 0);
  }
}

TEST(LlvmLibcFScanfTest, IntegerWidthDoesNotProbeEof) {
  struct {
    const char *input;
    const char *format;
    int value;
  } cases[] = {{"7", "%1d%n", 7},    {"-7", "%2d%n", -7}, {"0", "%1i%n", 0},
               {"+0", "%2i%n", 0},   {"07", "%2i%n", 7},  {"0x7", "%3i%n", 7},
               {"-0X7", "%4i%n", -7}};
  auto path = libc_make_test_file_path("scanf_integer_width.test");
  for (const auto &test : cases) {
    LIBC_NAMESPACE::cpp::string_view input(test.input);
    ::FILE *file = scanf_test::fopen(path, "w");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(scanf_test::fwrite(input.data(), 1, input.size(), file),
              input.size());
    ASSERT_EQ(scanf_test::fclose(file), 0);
    file = scanf_test::fopen(path, "r");
    ASSERT_NE(file, nullptr);
    int value = 37;
    int count = -1;
    EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, test.format, &value, &count), 1);
    EXPECT_EQ(value, test.value);
    EXPECT_EQ(count, int(input.size()));
    EXPECT_EQ(scanf_test::feof(file), 0);
    EXPECT_EQ(scanf_test::ferror(file), 0);
    char next = '?';
    EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%c", &next), EOF);
    EXPECT_EQ(next, '?');
    EXPECT_NE(scanf_test::feof(file), 0);
    EXPECT_EQ(scanf_test::ferror(file), 0);
    EXPECT_EQ(scanf_test::fclose(file), 0);
  }
}

TEST(LlvmLibcFScanfTest, IncompleteMantissaPreservesNextByte) {
  auto path = libc_make_test_file_path("scanf_incomplete_mantissa.test");
  ::FILE *file = scanf_test::fopen(path, "w");
  ASSERT_NE(file, nullptr);
  ASSERT_EQ(scanf_test::fwrite(".e1!", 1, 4, file), size_t(4));
  ASSERT_EQ(scanf_test::fclose(file), 0);
  file = scanf_test::fopen(path, "r");
  ASSERT_NE(file, nullptr);
  float value = 19;
  EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%f", &value), 0);
  EXPECT_TRUE(value == 19.0f);
  char next = '?';
  EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%c", &next), 1);
  EXPECT_EQ(next, 'e');
  EXPECT_EQ(scanf_test::fclose(file), 0);
}

TEST(LlvmLibcFScanfTest, FloatWidthDoesNotProbeEof) {
  auto path = libc_make_test_file_path("scanf_float_width.test");
  ::FILE *file = scanf_test::fopen(path, "w");
  ASSERT_NE(file, nullptr);
  ASSERT_EQ(scanf_test::fwrite("1.5", 1, 3, file), size_t(3));
  ASSERT_EQ(scanf_test::fclose(file), 0);
  file = scanf_test::fopen(path, "r");
  ASSERT_NE(file, nullptr);
  float value = 0;
  EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%3f", &value), 1);
  EXPECT_TRUE(value == 1.5f);
  EXPECT_EQ(scanf_test::feof(file), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%f", &value), EOF);
  EXPECT_EQ(scanf_test::fclose(file), 0);
}

TEST(LlvmLibcFScanfTest, FailedFloatPreservesLookahead) {
  auto path = libc_make_test_file_path("scanf_float_lookahead.test");
  ::FILE *file = scanf_test::fopen(path, "w");
  ASSERT_NE(file, nullptr);
  constexpr char input[] = "0x1p 12";
  ASSERT_EQ(scanf_test::fwrite(input, 1, sizeof(input) - 1, file),
            sizeof(input) - 1);
  ASSERT_EQ(scanf_test::fclose(file), 0);
  file = scanf_test::fopen(path, "r");
  ASSERT_NE(file, nullptr);
  double value = -1;
  int count = -1;
  EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%lf%n", &value, &count), 0);
  EXPECT_EQ(count, -1);
  EXPECT_EQ(scanf_test::feof(file), 0);
  char next = 0;
  EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%c", &next), 1);
  EXPECT_EQ(next, ' ');
  EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%*d%n", &count), 0);
  EXPECT_EQ(count, 2);
  EXPECT_NE(scanf_test::feof(file), 0);
  EXPECT_EQ(scanf_test::fclose(file), 0);
}

TEST(LlvmLibcFScanfTest, EmbeddedNullAndHighBytes) {
  auto path = libc_make_test_file_path("scanf_binary.test");
  ::FILE *file = scanf_test::fopen(path, "w");
  ASSERT_NE(file, nullptr);
  constexpr char input[] = {'A', '\0', static_cast<char>(0xff), 'B'};
  ASSERT_EQ(scanf_test::fwrite(input, 1, sizeof(input), file), sizeof(input));
  ASSERT_EQ(scanf_test::fclose(file), 0);
  file = scanf_test::fopen(path, "r");
  ASSERT_NE(file, nullptr);
  char output[4] = {};
  int count = -1;
  EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%4c%n", output, &count), 1);
  EXPECT_EQ(count, 4);
  for (size_t i = 0; i < sizeof(input); ++i)
    EXPECT_EQ(output[i], input[i]);
  // A complete fixed-width conversion must not probe beyond its input item.
  EXPECT_EQ(scanf_test::feof(file), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%c", output), EOF);
  EXPECT_EQ(scanf_test::fclose(file), 0);
}

TEST(LlvmLibcFScanfTest, StringWidthDoesNotProbeEof) {
  auto path = libc_make_test_file_path("scanf_string_width.test");
  ::FILE *file = scanf_test::fopen(path, "w");
  ASSERT_NE(file, nullptr);
  ASSERT_EQ(scanf_test::fwrite("A", 1, 1, file), size_t(1));
  ASSERT_EQ(scanf_test::fclose(file), 0);
  const char *formats[] = {"%1s%n", "%1[A]%n", "%*1s%n", "%*1[A]%n"};
  for (size_t i = 0; i < 4; ++i) {
    file = scanf_test::fopen(path, "r");
    ASSERT_NE(file, nullptr);
    char output[2] = {'?', '?'};
    int count = -1;
    if (i < 2) {
      EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, formats[i], output, &count), 1);
      EXPECT_STREQ(output, "A");
    } else {
      EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, formats[i], &count), 0);
    }
    EXPECT_EQ(count, 1);
    EXPECT_EQ(scanf_test::feof(file), 0);
    EXPECT_EQ(LIBC_NAMESPACE::fscanf(file, "%c", output), EOF);
    EXPECT_EQ(scanf_test::fclose(file), 0);
  }
}

TEST(LlvmLibcFScanfTest, WriteToFile) {
  const char *FILENAME = "fscanf_output.test";
  auto FILE_PATH = libc_make_test_file_path(FILENAME);
  ::FILE *file = scanf_test::fopen(FILE_PATH, "w");
  ASSERT_FALSE(file == nullptr);

  int read;

  constexpr char simple[] = "A simple string with no conversions.\n";

  ASSERT_EQ(sizeof(simple) - 1,
            scanf_test::fwrite(simple, 1, sizeof(simple) - 1, file));

  constexpr char numbers[] = "1234567890\n";

  ASSERT_EQ(sizeof(numbers) - 1,
            scanf_test::fwrite(numbers, 1, sizeof(numbers) - 1, file));

  constexpr char numbers_and_more[] = "1234 and more\n";

  ASSERT_EQ(sizeof(numbers_and_more) - 1,
            scanf_test::fwrite(numbers_and_more, 1,
                               sizeof(numbers_and_more) - 1, file));

  read = LIBC_NAMESPACE::fscanf(file,
                                "Reading from a write-only file should fail.");
  EXPECT_NE(scanf_test::ferror(file), 0);
  EXPECT_EQ(read, EOF);

  ASSERT_EQ(0, scanf_test::fclose(file));

  file = scanf_test::fopen(FILE_PATH, "r");
  ASSERT_FALSE(file == nullptr);

  char data[50];
  read = LIBC_NAMESPACE::fscanf(file, "%[A-Za-z .\n]", data);
  ASSERT_EQ(read, 1);
  ASSERT_STREQ(simple, data);

  read = LIBC_NAMESPACE::fscanf(file, "%s", data);
  ASSERT_EQ(read, 1);
  ASSERT_EQ(LIBC_NAMESPACE::cpp::string_view(numbers, 10),
            LIBC_NAMESPACE::cpp::string_view(data));

  // The format string starts with a space to handle the fact that the %s leaves
  // a trailing \n and %c doesn't strip leading whitespace.
  read = LIBC_NAMESPACE::fscanf(file, " %50c", data);
  // %c requires the full field width, not merely a nonempty prefix.
  ASSERT_EQ(read, EOF);
  ASSERT_EQ(
      LIBC_NAMESPACE::cpp::string_view(numbers_and_more),
      LIBC_NAMESPACE::cpp::string_view(data, sizeof(numbers_and_more) - 1));

  ASSERT_EQ(scanf_test::ferror(file), 0);
  ASSERT_EQ(scanf_test::fclose(file), 0);
}

TEST(LlvmLibcFScanfTest, ProcNetIfInet6Sample) {
  const char *FILENAME = "proc_net_if_inet6.txt";
  auto FILE_PATH = libc_make_test_file_path(FILENAME);
  ::FILE *file = scanf_test::fopen(FILE_PATH, "w");
  ASSERT_FALSE(file == nullptr);

  // Sample contents of /proc/net/if_inet6 on Linux.
  constexpr char sample_contents[] =
      "fe80000000000000adaf264669baa4c7 02 40 20 80     ens4\n"
      "00000000000000000000000000000001 01 80 10 80       lo\n";
  constexpr char entry_format[] =
      "%4s%4s%4s%4s%4s%4s%4s%4s %08x %02x %02x %02x %20s\n";

  ASSERT_EQ(sizeof(sample_contents) - 1,
            scanf_test::fwrite(sample_contents, 1, sizeof(sample_contents) - 1,
                               file));
  ASSERT_EQ(0, scanf_test::fclose(file));

  file = scanf_test::fopen(FILE_PATH, "r");
  ASSERT_FALSE(file == nullptr);

  char addr6p[8][5];
  int if_idx, prefix, scope, dad_status;
  char devname[21];

  // Validate first entry.
  int ret = LIBC_NAMESPACE::fscanf(file, entry_format, addr6p[0], addr6p[1],
                                   addr6p[2], addr6p[3], addr6p[4], addr6p[5],
                                   addr6p[6], addr6p[7], &if_idx, &prefix,
                                   &scope, &dad_status, devname);
  ASSERT_EQ(ret, 13);
  EXPECT_STREQ(addr6p[0], "fe80");
  EXPECT_STREQ(addr6p[1], "0000");
  EXPECT_STREQ(addr6p[2], "0000");
  EXPECT_STREQ(addr6p[3], "0000");
  EXPECT_STREQ(addr6p[4], "adaf");
  EXPECT_STREQ(addr6p[5], "2646");
  EXPECT_STREQ(addr6p[6], "69ba");
  EXPECT_STREQ(addr6p[7], "a4c7");
  EXPECT_EQ(if_idx, 2);
  EXPECT_EQ(prefix, 64);
  EXPECT_EQ(scope, 32);
  EXPECT_EQ(dad_status, 128);
  EXPECT_STREQ(devname, "ens4");

  // Validate second entry.
  ret = LIBC_NAMESPACE::fscanf(file, entry_format, addr6p[0], addr6p[1],
                               addr6p[2], addr6p[3], addr6p[4], addr6p[5],
                               addr6p[6], addr6p[7], &if_idx, &prefix, &scope,
                               &dad_status, devname);
  ASSERT_EQ(ret, 13);
  for (int i = 0; i < 7; i++)
    EXPECT_STREQ(addr6p[i], "0000");
  EXPECT_STREQ(addr6p[7], "0001");
  EXPECT_EQ(if_idx, 1);
  EXPECT_EQ(prefix, 128);
  EXPECT_EQ(scope, 16);
  EXPECT_EQ(dad_status, 128);
  EXPECT_STREQ(devname, "lo");

  // No more entries, return EOF.
  ret = LIBC_NAMESPACE::fscanf(file, entry_format, addr6p[0], addr6p[1],
                               addr6p[2], addr6p[3], addr6p[4], addr6p[5],
                               addr6p[6], addr6p[7], &if_idx, &prefix, &scope,
                               &dad_status, devname);
  EXPECT_EQ(ret, EOF);
  EXPECT_NE(scanf_test::feof(file), 0);
  EXPECT_EQ(scanf_test::ferror(file), 0);

  ASSERT_EQ(scanf_test::fclose(file), 0);
}

TEST(LlvmLibcFScanfTest, EofPartialMatch) {
  const char *FILENAME = "eof_partial_match.txt";
  auto FILE_PATH = libc_make_test_file_path(FILENAME);
  ::FILE *file = scanf_test::fopen(FILE_PATH, "w");
  ASSERT_FALSE(file == nullptr);

  constexpr char contents[] = "1 2 3";
  ASSERT_EQ(sizeof(contents) - 1,
            scanf_test::fwrite(contents, 1, sizeof(contents) - 1, file));
  ASSERT_EQ(0, scanf_test::fclose(file));

  file = scanf_test::fopen(FILE_PATH, "r");
  ASSERT_FALSE(file == nullptr);

  int vals[4] = {0};
  int ret = LIBC_NAMESPACE::fscanf(file, "%d %d %d %d", &vals[0], &vals[1],
                                   &vals[2], &vals[3]);
  // Returns 3 for number of matches despite EOF.
  EXPECT_EQ(ret, 3);
  EXPECT_EQ(vals[0], 1);
  EXPECT_EQ(vals[1], 2);
  EXPECT_EQ(vals[2], 3);

  EXPECT_NE(scanf_test::feof(file), 0);
  EXPECT_EQ(scanf_test::ferror(file), 0);

  ASSERT_EQ(scanf_test::fclose(file), 0);
}

TEST(LlvmLibcFScanfTest, MatchingErrors) {
  const char *FILENAME = "matching_error_partial_match.txt";
  auto FILE_PATH = libc_make_test_file_path(FILENAME);
  ::FILE *file = scanf_test::fopen(FILE_PATH, "w");
  ASSERT_FALSE(file == nullptr);

  constexpr char contents[] = "one is 1 two is 2";
  ASSERT_EQ(sizeof(contents) - 1,
            scanf_test::fwrite(contents, 1, sizeof(contents) - 1, file));
  ASSERT_EQ(0, scanf_test::fclose(file));

  file = scanf_test::fopen(FILE_PATH, "r");
  ASSERT_FALSE(file == nullptr);

  int vals[2] = {0};
  // Immediate matching error.
  int ret =
      LIBC_NAMESPACE::fscanf(file, "zzz is %d two is %d", &vals[0], &vals[1]);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(scanf_test::feof(file), 0);
  EXPECT_EQ(scanf_test::ferror(file), 0);

  // Only the first item is matched.
  ret = LIBC_NAMESPACE::fscanf(file, "one is %d zzz is %d", &vals[0], &vals[1]);
  EXPECT_EQ(ret, 1);
  EXPECT_EQ(vals[0], 1);
  EXPECT_EQ(scanf_test::feof(file), 0);
  EXPECT_EQ(scanf_test::ferror(file), 0);

  // Second item is matched before EOF.
  ret = LIBC_NAMESPACE::fscanf(file, "two is %d zzz is %d", &vals[0], &vals[1]);
  EXPECT_EQ(ret, 1);
  EXPECT_EQ(vals[0], 2);
  EXPECT_NE(scanf_test::feof(file), 0);
  EXPECT_EQ(scanf_test::ferror(file), 0);

  ASSERT_EQ(scanf_test::fclose(file), 0);
}
