//===-- Tests for the getopt_long entrypoint
//-------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/getopt/getopt_long.h"
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
