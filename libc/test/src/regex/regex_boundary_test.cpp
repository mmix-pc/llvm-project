//===-- Regular expression input boundary tests ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/regex_macros.h"
#include "hdr/sys_mman_macros.h"
#include "hdr/unistd_macros.h"
#include "src/regex/regcomp.h"
#include "src/regex/regexec.h"
#include "src/regex/regfree.h"
#include "src/sys/mman/mmap.h"
#include "src/sys/mman/mprotect.h"
#include "src/sys/mman/munmap.h"
#include "src/unistd/sysconf.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcRegexBoundaryTest, BackreferenceAnchorAtFirstByte) {
  long page = LIBC_NAMESPACE::sysconf(_SC_PAGESIZE);
  ASSERT_GT(page, 0L);
  size_t size = static_cast<size_t>(page);
  void *mapping =
      LIBC_NAMESPACE::mmap(nullptr, 2 * size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_NE(mapping, MAP_FAILED);
  struct Mapping {
    void *p;
    size_t size;
    ~Mapping() { LIBC_NAMESPACE::munmap(p, size); }
  } owner{mapping, size * 2};
  ASSERT_EQ(LIBC_NAMESPACE::mprotect(mapping, size, PROT_NONE), 0);
  char *input = static_cast<char *>(mapping) + size;
  input[0] = input[1] = 'a';
  input[2] = '\0';
  const int flags[] = {0, REG_NEWLINE};
  const char *patterns[] = {"\\(^a\\)*\\(a\\)\\2", "\\([[:<:]]a\\)*\\(a\\)\\2"};
  for (const char *pattern : patterns) {
    for (int flag : flags) {
      regex_t re;
      // The optional anchored group is tried before the zero-repeat
      // alternative. A real backreference forces the engine's backtracking
      // path.
      ASSERT_EQ(LIBC_NAMESPACE::regcomp(&re, pattern, flag), 0);
      regmatch_t matches[3];
      int result = LIBC_NAMESPACE::regexec(&re, input, 3, matches, REG_NOTBOL);
      LIBC_NAMESPACE::regfree(&re);
      ASSERT_EQ(result, 0);
      EXPECT_EQ(matches[0].rm_so, regoff_t(0));
      EXPECT_EQ(matches[0].rm_eo, regoff_t(2));
      EXPECT_EQ(matches[1].rm_so, regoff_t(-1));
      EXPECT_EQ(matches[2].rm_so, regoff_t(0));
      EXPECT_EQ(matches[2].rm_eo, regoff_t(1));
    }
  }
}
