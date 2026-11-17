//===-- Tests for the getopt_long entrypoint
//-------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/getopt/getopt_long.h"
#include "src/getopt/getopt_long_only.h"
#include "src/stdlib/setenv.h"
#include "src/stdlib/unsetenv.h"
#include "src/unistd/getopt.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcGetoptLongTest, EnvironmentAndPublicState) {
  using namespace LIBC_NAMESPACE;
  char program[] = "prog", operand[] = "operand", option[] = "--alpha";
  char *argv[] = {program, operand, option, nullptr};
  const struct option options[] = {{"alpha", 0, nullptr, 'a'},
                                   {nullptr, 0, nullptr, 0}};
  ASSERT_EQ(setenv("POSIXLY_CORRECT", "", 1), 0);
  optind = 0;
  EXPECT_EQ(getopt_long(3, argv, "a", options, nullptr), -1);
  EXPECT_EQ(optind, 1);
  ASSERT_EQ(unsetenv("POSIXLY_CORRECT"), 0);
  optind = 0;
  EXPECT_EQ(getopt_long(3, argv, "a", options, nullptr), int('a'));
  EXPECT_EQ(getopt_long(3, argv, "a", options, nullptr), -1);
  EXPECT_EQ(optind, 2);
  EXPECT_STREQ(argv[1], "--alpha");
  EXPECT_STREQ(argv[2], "operand");
  EXPECT_TRUE(optarg == nullptr);
  EXPECT_EQ(opterr, 1);
}

TEST(LlvmLibcGetoptLongTest, LongOnlyShortPrecedenceAndFallback) {
  using namespace LIBC_NAMESPACE;
  ASSERT_EQ(unsetenv("POSIXLY_CORRECT"), 0);
  char program[] = "prog", a[] = "-a", alpha[] = "-alpha", cluster[] = "-ab",
       abbreviated[] = "-bet=value";
  char *argv[] = {program, a, alpha, cluster, abbreviated, nullptr};
  const struct option options[] = {{"alpha", 0, nullptr, 100},
                                   {"beta", 1, nullptr, 101},
                                   {nullptr, 0, nullptr, 0}};
  optind = 0;
  int index = -1;
  EXPECT_EQ(getopt_long_only(5, argv, "ab", options, &index), int('a'));
  EXPECT_EQ(index, -1);
  EXPECT_EQ(getopt_long_only(5, argv, "ab", options, &index), 100);
  EXPECT_EQ(index, 0);
  EXPECT_EQ(getopt_long_only(5, argv, "ab", options, nullptr), int('a'));
  EXPECT_EQ(getopt_long_only(5, argv, "ab", options, nullptr), int('b'));
  EXPECT_EQ(getopt_long_only(5, argv, "ab", options, &index), 101);
  EXPECT_STREQ(optarg, "value");
  EXPECT_EQ(index, 1);
  EXPECT_EQ(getopt_long_only(5, argv, "ab", options, nullptr), -1);
}

TEST(LlvmLibcGetoptLongTest, LongOnlyAmbiguityAndMissingArgument) {
  using namespace LIBC_NAMESPACE;
  char program[] = "prog", abbr[] = "-al", required[] = "-required";
  char *argv[] = {program, abbr, required, nullptr};
  const struct option options[] = {{"alpha", 0, nullptr, 100},
                                   {"alpine", 0, nullptr, 100},
                                   {"required", 1, nullptr, 101},
                                   {nullptr, 0, nullptr, 0}};
  optind = 0;
  opterr = 0;
  int index = -1;
  EXPECT_EQ(getopt_long_only(3, argv, ":a", options, &index), int('?'));
  EXPECT_EQ(optopt, 0);
  EXPECT_EQ(index, -1);
  EXPECT_EQ(getopt_long_only(3, argv, ":a", options, &index), int(':'));
  EXPECT_EQ(optopt, 101);
  EXPECT_EQ(index, -1);
  opterr = 1;
  // Ordinary getopt_long keeps its established equivalent-alias rule.
  char two_dash[] = "--al";
  char *ordinary[] = {program, two_dash, nullptr};
  optind = 0;
  EXPECT_EQ(getopt_long(2, ordinary, "", options, &index), 100);
}
