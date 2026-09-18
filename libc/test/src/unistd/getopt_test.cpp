//===-- Unittests for getopt ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/getopt.h"
#include "test/UnitTest/Test.h"

#include "src/__support/CPP/array.h"
#include "src/stdio/fflush.h"
#include "src/stdio/fopencookie.h"

using LIBC_NAMESPACE::cpp::array;

namespace test_globals {
char *optarg;
int optind = 1;
int optopt;
int opterr = 1;

unsigned optpos;
} // namespace test_globals

void set_state(FILE *errstream) {
  LIBC_NAMESPACE::impl::set_getopt_state(
      &test_globals::optarg, &test_globals::optind, &test_globals::optopt,
      &test_globals::optpos, &test_globals::opterr, errstream);
}

static void my_memcpy(char *dest, const char *src, size_t size) {
  for (size_t i = 0; i < size; i++)
    dest[i] = src[i];
}

ssize_t cookie_write(void *cookie, const char *buf, size_t size) {
  char **pos = static_cast<char **>(cookie);
  my_memcpy(*pos, buf, size);
  *pos += size;
  return size;
}

static cookie_io_functions_t cookie{nullptr, &cookie_write, nullptr, nullptr};

// TODO: <stdio> could be either llvm-libc's or the system libc's. The former
// doesn't currently support fmemopen but does have fopencookie. In the future
// just use that instead. This memopen does no error checking for the size
// of the buffer, etc.
FILE *memopen(char **pos) {
  return LIBC_NAMESPACE::fopencookie(pos, "w", cookie);
}

struct LlvmLibcGetoptTest : public LIBC_NAMESPACE::testing::Test {
  FILE *errstream;
  char buf[256] = {};
  char *pos = buf;

  void reset_errstream() {
    pos = buf;
    *pos = '\0';
  }
  const char *get_error_msg() {
    LIBC_NAMESPACE::fflush(errstream);
    *pos = '\0';
    return buf;
  }

  void SetUp() override {
    ASSERT_TRUE(!!(errstream = memopen(&pos)));
    set_state(errstream);
    ASSERT_EQ(test_globals::optind, 1);
  }

  void TearDown() override {
    test_globals::optind = 1;
    test_globals::opterr = 1;
  }
};

// This is safe because getopt doesn't currently permute argv like GNU's getopt
// does so this just helps silence warnings.
char *operator""_c(const char *c, size_t) { return const_cast<char *>(c); }

TEST_F(LlvmLibcGetoptTest, NoMatch) {
  array<char *, 3> argv{"prog"_c, "arg1"_c, nullptr};

  // optind >= argc
  EXPECT_EQ(LIBC_NAMESPACE::getopt(1, argv.data(), "..."), -1);

  // argv[optind] == nullptr
  test_globals::optind = 2;
  EXPECT_EQ(LIBC_NAMESPACE::getopt(100, argv.data(), "..."), -1);

  // argv[optind][0] != '-'
  test_globals::optind = 1;
  EXPECT_EQ(LIBC_NAMESPACE::getopt(2, argv.data(), "a"), -1);
  ASSERT_EQ(test_globals::optind, 1);

  // argv[optind] == "-"
  argv[1] = "-"_c;
  set_state(errstream);
  EXPECT_EQ(LIBC_NAMESPACE::getopt(2, argv.data(), "a"), -1);
  ASSERT_EQ(test_globals::optind, 1);

  // argv[optind] == "--", then return -1 and incremement optind
  argv[1] = "--"_c;
  set_state(errstream);
  EXPECT_EQ(LIBC_NAMESPACE::getopt(2, argv.data(), "a"), -1);
  EXPECT_EQ(test_globals::optind, 2);
}

TEST_F(LlvmLibcGetoptTest, WrongMatch) {
  array<char *, 3> argv{"prog"_c, "-b"_c, nullptr};

  EXPECT_EQ(LIBC_NAMESPACE::getopt(2, argv.data(), "a"), int('?'));
  EXPECT_EQ(test_globals::optopt, (int)'b');
  EXPECT_EQ(test_globals::optind, 2);
  EXPECT_STREQ(get_error_msg(), "prog: illegal option -- b\n");
}

TEST_F(LlvmLibcGetoptTest, OpterrFalse) {
  array<char *, 3> argv{"prog"_c, "-b"_c, nullptr};

  test_globals::opterr = 0;
  set_state(errstream);
  EXPECT_EQ(LIBC_NAMESPACE::getopt(2, argv.data(), "a"), int('?'));
  EXPECT_EQ(test_globals::optopt, (int)'b');
  EXPECT_EQ(test_globals::optind, 2);
  EXPECT_STREQ(get_error_msg(), "");
}

TEST_F(LlvmLibcGetoptTest, MissingArg) {
  array<char *, 3> argv{"prog"_c, "-b"_c, nullptr};

  EXPECT_EQ(LIBC_NAMESPACE::getopt(2, argv.data(), ":b:"), (int)':');
  ASSERT_EQ(test_globals::optind, 2);
  EXPECT_EQ(test_globals::optopt, int('b'));
  EXPECT_STREQ(get_error_msg(), "");
  reset_errstream();
  test_globals::optind = 0;
  EXPECT_EQ(LIBC_NAMESPACE::getopt(2, argv.data(), "b:"), int('?'));
  EXPECT_EQ(test_globals::optind, 2);
  EXPECT_STREQ(get_error_msg(), "prog: option requires an argument -- b\n");
}

TEST_F(LlvmLibcGetoptTest, ParseArgInCurrent) {
  array<char *, 3> argv{"prog"_c, "-barg"_c, nullptr};

  EXPECT_EQ(LIBC_NAMESPACE::getopt(2, argv.data(), "b:"), (int)'b');
  EXPECT_STREQ(test_globals::optarg, "arg");
  EXPECT_EQ(test_globals::optind, 2);
}

TEST_F(LlvmLibcGetoptTest, ParseArgInNext) {
  array<char *, 4> argv{"prog"_c, "-b"_c, "arg"_c, nullptr};

  EXPECT_EQ(LIBC_NAMESPACE::getopt(3, argv.data(), "b:"), (int)'b');
  EXPECT_STREQ(test_globals::optarg, "arg");
  EXPECT_EQ(test_globals::optind, 3);
}

TEST_F(LlvmLibcGetoptTest, ParseMultiInOne) {
  array<char *, 3> argv{"prog"_c, "-abc"_c, nullptr};

  EXPECT_EQ(LIBC_NAMESPACE::getopt(2, argv.data(), "abc"), (int)'a');
  ASSERT_EQ(test_globals::optind, 1);
  EXPECT_EQ(LIBC_NAMESPACE::getopt(2, argv.data(), "abc"), (int)'b');
  ASSERT_EQ(test_globals::optind, 1);
  EXPECT_EQ(LIBC_NAMESPACE::getopt(2, argv.data(), "abc"), (int)'c');
  EXPECT_EQ(test_globals::optind, 2);
}

TEST_F(LlvmLibcGetoptTest, ResetAndErrorsAdvance) {
  array<char *, 4> argv{"prog"_c, "-xay"_c, "-bvalue"_c, nullptr};
  test_globals::opterr = 0;
  EXPECT_EQ(LIBC_NAMESPACE::getopt(3, argv.data(), "ab:"), int('?'));
  EXPECT_EQ(test_globals::optopt, int('x'));
  EXPECT_EQ(LIBC_NAMESPACE::getopt(3, argv.data(), "ab:"), int('a'));
  EXPECT_EQ(LIBC_NAMESPACE::getopt(3, argv.data(), "ab:"), int('?'));
  EXPECT_EQ(test_globals::optopt, int('y'));
  EXPECT_EQ(test_globals::optind, 2);
  EXPECT_EQ(LIBC_NAMESPACE::getopt(3, argv.data(), "ab:"), int('b'));
  EXPECT_STREQ(test_globals::optarg, "value");
  EXPECT_EQ(LIBC_NAMESPACE::getopt(3, argv.data(), "ab:"), -1);
  EXPECT_TRUE(test_globals::optarg == nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::getopt(3, argv.data(), "ab:"), -1);
  test_globals::optind = 0;
  EXPECT_EQ(LIBC_NAMESPACE::getopt(3, argv.data(), "xayb:"), int('x'));
  test_globals::optind = 0;
  EXPECT_EQ(LIBC_NAMESPACE::getopt(3, argv.data(), ""), int('?'));
}

TEST_F(LlvmLibcGetoptTest, OptionalShortAndHighByte) {
  array<char *, 5> argv{"prog"_c, "-aattached"_c, "-a"_c, "operand"_c, nullptr};
  EXPECT_EQ(LIBC_NAMESPACE::getopt(4, argv.data(), "a::"), int('a'));
  EXPECT_STREQ(test_globals::optarg, "attached");
  EXPECT_EQ(LIBC_NAMESPACE::getopt(4, argv.data(), "a::"), int('a'));
  EXPECT_TRUE(test_globals::optarg == nullptr);
  EXPECT_EQ(LIBC_NAMESPACE::getopt(4, argv.data(), "a::"), -1);
  EXPECT_EQ(test_globals::optind, 3);
  test_globals::optind = 0;
  argv[1] = "-\x80"_c;
  EXPECT_EQ(LIBC_NAMESPACE::getopt(2, argv.data(), "\x80"), 128);
}

// Exercise the shared engine without coupling parser tests to process environ.
static int parse_long(int argc, char *const argv[], const char *shorts,
                      const struct option *options, int *index = nullptr,
                      bool permute = true) {
  return LIBC_NAMESPACE::impl::getopt_internal(argc, argv, shorts, options,
                                               index, permute);
}

TEST_F(LlvmLibcGetoptTest, LongArgumentsAndFlag) {
  int flag = 0, index = -1;
  const struct option options[] = {{"alpha", 0, &flag, 19},
                                   {"beta", 1, nullptr, 'b'},
                                   {"optional", 2, nullptr, 'o'},
                                   {nullptr, 0, nullptr, 0}};
  array<char *, 9> argv{"prog"_c,       "--alpha"_c, "--beta="_c,
                        "--beta"_c,     "-value"_c,  "--optional=arg"_c,
                        "--optional"_c, "operand"_c, nullptr};
  EXPECT_EQ(parse_long(8, argv.data(), "", options, &index), 0);
  EXPECT_EQ(flag, 19);
  EXPECT_EQ(index, 0);
  EXPECT_EQ(parse_long(8, argv.data(), "", options, &index), int('b'));
  EXPECT_STREQ(test_globals::optarg, "");
  EXPECT_EQ(index, 1);
  EXPECT_EQ(parse_long(8, argv.data(), "", options), int('b'));
  EXPECT_STREQ(test_globals::optarg, "-value");
  EXPECT_EQ(parse_long(8, argv.data(), "", options), int('o'));
  EXPECT_STREQ(test_globals::optarg, "arg");
  EXPECT_EQ(parse_long(8, argv.data(), "", options), int('o'));
  EXPECT_TRUE(test_globals::optarg == nullptr);
  EXPECT_EQ(parse_long(8, argv.data(), "", options), -1);
  EXPECT_EQ(test_globals::optind, 7);
}

TEST_F(LlvmLibcGetoptTest, LongAbbreviationAndErrors) {
  const struct option options[] = {{"alpha", 0, nullptr, 'a'},
                                   {"alphabet", 0, nullptr, 'b'},
                                   {"beta", 1, nullptr, 'c'},
                                   {nullptr, 0, nullptr, 0}};
  array<char *, 8> argv{"prog"_c,       "--alph"_c,    "--alpha"_c,
                        "--alphabet"_c, "--missing"_c, "--alpha=x"_c,
                        "--bet"_c,      nullptr};
  int index = -1;
  EXPECT_EQ(parse_long(7, argv.data(), ":", options, &index), int('?'));
  EXPECT_EQ(test_globals::optopt, 0);
  EXPECT_EQ(index, -1);
  EXPECT_EQ(parse_long(7, argv.data(), ":", options, &index), int('a'));
  EXPECT_EQ(index, 0);
  EXPECT_EQ(parse_long(7, argv.data(), ":", options), int('b'));
  EXPECT_EQ(parse_long(7, argv.data(), ":", options), int('?'));
  EXPECT_EQ(test_globals::optopt, 0);
  EXPECT_EQ(parse_long(7, argv.data(), ":", options), int('?'));
  EXPECT_EQ(test_globals::optopt, int('a'));
  EXPECT_EQ(parse_long(7, argv.data(), ":", options), int(':'));
  EXPECT_EQ(test_globals::optopt, int('c'));
  EXPECT_EQ(test_globals::optind, 7);
  EXPECT_EQ(parse_long(7, argv.data(), ":", options), -1);
  EXPECT_STREQ(get_error_msg(), "");
}

TEST_F(LlvmLibcGetoptTest, LongAliasesAndDiagnostics) {
  const struct option options[] = {{"colour", 0, nullptr, 128},
                                   {"color", 0, nullptr, 128},
                                   {"required", 1, nullptr, 'r'},
                                   {nullptr, 0, nullptr, 0}};
  array<char *, 4> argv{"prog"_c, "--col"_c, "--req"_c, nullptr};
  EXPECT_EQ(parse_long(3, argv.data(), "", options), 128);
  EXPECT_EQ(parse_long(3, argv.data(), "", options), int('?'));
  EXPECT_STREQ(get_error_msg(),
               "prog: option requires an argument -- required\n");
  EXPECT_EQ(test_globals::optind, 3);
}

TEST_F(LlvmLibcGetoptTest, StablePermutationAndTerminator) {
  const struct option options[] = {{"beta", 1, nullptr, 'b'},
                                   {nullptr, 0, nullptr, 0}};
  array<char *, 11> argv{"prog"_c,   "one"_c,   "-ac"_c, "two"_c,
                         "--beta"_c, "arg"_c,   "-"_c,   "--"_c,
                         "-a"_c,     "three"_c, nullptr};
  EXPECT_EQ(parse_long(10, argv.data(), "ac", options), int('a'));
  EXPECT_EQ(parse_long(10, argv.data(), "ac", options), int('c'));
  EXPECT_EQ(parse_long(10, argv.data(), "ac", options), int('b'));
  EXPECT_STREQ(test_globals::optarg, "arg");
  EXPECT_EQ(parse_long(10, argv.data(), "ac", options), -1);
  EXPECT_EQ(test_globals::optind, 5);
  array<const char *, 10> expected{"prog", "-ac", "--beta", "arg", "--",
                                   "one",  "two", "-",      "-a",  "three"};
  for (unsigned i = 0; i < expected.size(); ++i)
    EXPECT_STREQ(argv[i], expected[i]);
  EXPECT_EQ(parse_long(10, argv.data(), "ac", options), -1);
  EXPECT_EQ(test_globals::optind, 5);
}

TEST_F(LlvmLibcGetoptTest, OrderingAndRestart) {
  const struct option options[] = {{nullptr, 0, nullptr, 0}};
  array<char *, 5> argv{"prog"_c, "operand"_c, "-a"_c, "last"_c, nullptr};
  EXPECT_EQ(parse_long(4, argv.data(), "+a", options), -1);
  EXPECT_EQ(test_globals::optind, 1);
  test_globals::optind = 0;
  EXPECT_EQ(parse_long(4, argv.data(), "a", options, nullptr, false), -1);
  EXPECT_EQ(test_globals::optind, 1);
  test_globals::optind = 0;
  EXPECT_EQ(parse_long(4, argv.data(), "-a", options), 1);
  EXPECT_STREQ(test_globals::optarg, "operand");
  EXPECT_EQ(parse_long(4, argv.data(), "-a", options), int('a'));
  EXPECT_TRUE(test_globals::optarg == nullptr);
  EXPECT_EQ(parse_long(4, argv.data(), "-a", options), 1);
  EXPECT_STREQ(test_globals::optarg, "last");
  EXPECT_EQ(parse_long(4, argv.data(), "-a", options), -1);
  test_globals::optind = 0;
  EXPECT_EQ(parse_long(4, argv.data(), "a", options), int('a'));
  EXPECT_EQ(parse_long(4, argv.data(), "a", options), -1);
  EXPECT_EQ(test_globals::optind, 2);
  EXPECT_STREQ(argv[1], "-a");
  EXPECT_STREQ(argv[2], "operand");
  EXPECT_STREQ(argv[3], "last");
}
