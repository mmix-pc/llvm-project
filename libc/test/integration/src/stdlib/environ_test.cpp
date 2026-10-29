//===-- Tests for application replacement of environ ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/clearenv.h"
#include "src/stdlib/getenv.h"
#include "src/stdlib/putenv.h"
#include "src/stdlib/setenv.h"
#include "src/stdlib/unsetenv.h"
#include "src/unistd/environ.h"
#include "test/IntegrationTest/test.h"

TEST_MAIN() {
  char first[] = "LLVM_LIBC_EXTERNAL_FIRST=one";
  char last[] = "LLVM_LIBC_EXTERNAL_LAST=two";
  char *external[] = {first, last, nullptr};
  LIBC_NAMESPACE::environ = external;
  // Must work before any environment function has initialized the manager.
  ASSERT_STREQ(LIBC_NAMESPACE::getenv("LLVM_LIBC_EXTERNAL_LAST"), "two");
  ASSERT_EQ(LIBC_NAMESPACE::setenv("LLVM_LIBC_EXTERNAL_FIRST", "ignored", 0),
            0);
  ASSERT_TRUE(LIBC_NAMESPACE::environ == external);
  ASSERT_EQ(LIBC_NAMESPACE::setenv("OWNED", "keep", 1), 0);

  char *value = LIBC_NAMESPACE::getenv("OWNED");
  ASSERT_TRUE(value != nullptr);
  char *owned = value - 6; // Include "OWNED=".

  char borrowed[] = "BORROWED=old";
  char *replacement[] = {owned, owned, borrowed, nullptr};
  LIBC_NAMESPACE::environ = replacement;
  ASSERT_TRUE(LIBC_NAMESPACE::getenv("LLVM_LIBC_EXTERNAL_FIRST") == nullptr);
  ASSERT_EQ(LIBC_NAMESPACE::putenv(borrowed), 0);
  borrowed[9] = 'O';
  ASSERT_TRUE(LIBC_NAMESPACE::getenv("BORROWED") == borrowed + 9);
  ASSERT_STREQ(LIBC_NAMESPACE::getenv("BORROWED"), "Old");
  ASSERT_STREQ(LIBC_NAMESPACE::getenv("OWNED"), "keep");
  ASSERT_EQ(LIBC_NAMESPACE::unsetenv("OWNED"), 0);
  ASSERT_TRUE(LIBC_NAMESPACE::getenv("OWNED") == nullptr);

  // Neither restore nor inspect replacement: its owned pointers were just
  // invalidated by unsetenv. Use a fresh borrowed array instead.
  char duplicate1[] = "DUP=one", duplicate2[] = "DUP=two";
  char *duplicates[] = {duplicate1, duplicate2, borrowed, nullptr};
  LIBC_NAMESPACE::environ = duplicates;
  ASSERT_EQ(LIBC_NAMESPACE::unsetenv("DUP"), 0);
  ASSERT_TRUE(LIBC_NAMESPACE::getenv("DUP") == nullptr);
  ASSERT_STREQ(duplicate1, "DUP=one");
  ASSERT_STREQ(duplicate2, "DUP=two");
  char remove[] = "BORROWED";
  ASSERT_EQ(LIBC_NAMESPACE::putenv(remove), 0);
  ASSERT_TRUE(LIBC_NAMESPACE::getenv("BORROWED") == nullptr);

  LIBC_NAMESPACE::environ = nullptr;
  ASSERT_TRUE(LIBC_NAMESPACE::getenv("BORROWED") == nullptr);
  ASSERT_EQ(LIBC_NAMESPACE::setenv("AFTER_NULL", "value", 1), 0);
  ASSERT_STREQ(LIBC_NAMESPACE::getenv("AFTER_NULL"), "value");
  LIBC_NAMESPACE::environ = external;
  ASSERT_EQ(LIBC_NAMESPACE::clearenv(), 0);
  ASSERT_TRUE(LIBC_NAMESPACE::environ == nullptr);
  ASSERT_STREQ(first, "LLVM_LIBC_EXTERNAL_FIRST=one");
  ASSERT_STREQ(last, "LLVM_LIBC_EXTERNAL_LAST=two");
  ASSERT_STREQ(borrowed, "BORROWED=Old");
  return 0;
}
