//===-- Integration tests for execl and execle ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/libc_errno.h"
#include "src/stdlib/getenv.h"
#include "src/sys/wait/waitpid.h"
#include "src/unistd/environ.h"
#include "src/unistd/execl.h"
#include "src/unistd/execle.h"
#include "src/unistd/fork.h"
#include "test/IntegrationTest/test.h"
#include <sys/wait.h>

TEST_MAIN(int argc, char **argv, char **) {
  if (argc > 1) {
    ASSERT_EQ(argc, 5);
    ASSERT_STREQ(argv[0], "custom-argv-zero");
    ASSERT_STREQ(argv[1], "child");
    ASSERT_STREQ(argv[3], "");
    ASSERT_STREQ(argv[4], "last");
    ASSERT_EQ(argv[5], nullptr);
    ASSERT_STREQ(LIBC_NAMESPACE::getenv("EXEC_LIST"), argv[2]);
    return 19;
  }

  char inherited[] = "EXEC_LIST=inherited";
  char explicit_env[] = "EXEC_LIST=explicit";
  char *inherited_env[] = {inherited, nullptr};
  char *env[] = {explicit_env, nullptr};
  char **saved = LIBC_NAMESPACE::environ;
  LIBC_NAMESPACE::environ = inherited_env;
  for (int explicit_mode = 0; explicit_mode != 2; ++explicit_mode) {
    pid_t pid = LIBC_NAMESPACE::fork();
    ASSERT_TRUE(pid >= 0);
    if (pid == 0) {
      if (explicit_mode)
        LIBC_NAMESPACE::execle("/proc/self/exe", "custom-argv-zero", "child",
                               "explicit", "", "last",
                               static_cast<char *>(nullptr), env);
      else
        LIBC_NAMESPACE::execl("/proc/self/exe", "custom-argv-zero", "child",
                              "inherited", "", "last",
                              static_cast<char *>(nullptr));
      LIBC_NAMESPACE::internal::exit(99);
    }
    int status;
    ASSERT_EQ(LIBC_NAMESPACE::waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 19);
  }
  LIBC_NAMESPACE::environ = saved;

  ASSERT_EQ(LIBC_NAMESPACE::execl("", "zero", static_cast<char *>(nullptr)),
            -1);
  ASSERT_EQ(libc_errno, ENOENT);
  ASSERT_EQ(
      LIBC_NAMESPACE::execle("", "zero", static_cast<char *>(nullptr), env),
      -1);
  ASSERT_EQ(libc_errno, ENOENT);
  // A null arg0 is already the terminator; do not read another argument.
  ASSERT_EQ(LIBC_NAMESPACE::execl("", static_cast<const char *>(nullptr)), -1);
  ASSERT_EQ(libc_errno, ENOENT);
  ASSERT_EQ(LIBC_NAMESPACE::execle("", static_cast<const char *>(nullptr), env),
            -1);
  ASSERT_EQ(libc_errno, ENOENT);
  return 0;
}
