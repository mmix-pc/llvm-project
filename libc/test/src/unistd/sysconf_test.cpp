//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unittests for sysconf
///
//===----------------------------------------------------------------------===//

#include "src/unistd/sysconf.h"
#include "test/UnitTest/Test.h"

#include <unistd.h>

TEST(LlvmLibcSysconfTest, PagesizeTest) {
  long pagesize = LIBC_NAMESPACE::sysconf(_SC_PAGESIZE);
  ASSERT_GT(pagesize, 0L);
}

TEST(LlvmLibcSysconfTest, NprocessorsConfTest) {
  long sysconf_count = LIBC_NAMESPACE::sysconf(_SC_NPROCESSORS_CONF);
  ASSERT_GT(sysconf_count, 0L);
}

TEST(LlvmLibcSysconfTest, NprocessorsOnlnTest) {
  long sysconf_count = LIBC_NAMESPACE::sysconf(_SC_NPROCESSORS_ONLN);
  ASSERT_GT(sysconf_count, 0L);
}

TEST(LlvmLibcSysconfTest, ThreadsTest) {
  long threads = LIBC_NAMESPACE::sysconf(_SC_THREADS);
  ASSERT_EQ(threads, _POSIX_THREADS);
}

TEST(LlvmLibcSysconfTest, ThreadOptionsTest) {
#if defined(_SC_THREAD_ATTR_STACKADDR) && defined(_POSIX_THREAD_ATTR_STACKADDR)
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_THREAD_ATTR_STACKADDR), long(_POSIX_THREAD_ATTR_STACKADDR));
#endif
#if defined(_SC_THREAD_ATTR_STACKSIZE) && defined(_POSIX_THREAD_ATTR_STACKSIZE)
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_THREAD_ATTR_STACKSIZE), long(_POSIX_THREAD_ATTR_STACKSIZE));
#endif
#if defined(_SC_THREAD_PRIORITY_SCHEDULING) && defined(_POSIX_THREAD_PRIORITY_SCHEDULING)
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_THREAD_PRIORITY_SCHEDULING), long(_POSIX_THREAD_PRIORITY_SCHEDULING));
#endif
#if defined(_SC_THREAD_PRIO_INHERIT) && defined(_POSIX_THREAD_PRIO_INHERIT)
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_THREAD_PRIO_INHERIT), long(_POSIX_THREAD_PRIO_INHERIT));
#endif
#if defined(_SC_THREAD_PRIO_PROTECT) && defined(_POSIX_THREAD_PRIO_PROTECT)
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_THREAD_PRIO_PROTECT), long(_POSIX_THREAD_PRIO_PROTECT));
#endif
#if defined(_SC_THREAD_PROCESS_SHARED) && defined(_POSIX_THREAD_PROCESS_SHARED)
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_THREAD_PROCESS_SHARED), long(_POSIX_THREAD_PROCESS_SHARED));
#endif
#if defined(_SC_THREAD_CPUTIME) && defined(_POSIX_THREAD_CPUTIME)
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_THREAD_CPUTIME), long(_POSIX_THREAD_CPUTIME));
#endif
#if defined(_SC_THREAD_ROBUST_PRIO_INHERIT) && defined(_POSIX_THREAD_ROBUST_PRIO_INHERIT)
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_THREAD_ROBUST_PRIO_INHERIT), long(_POSIX_THREAD_ROBUST_PRIO_INHERIT));
#endif
#if defined(_SC_THREAD_ROBUST_PRIO_PROTECT) && defined(_POSIX_THREAD_ROBUST_PRIO_PROTECT)
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_THREAD_ROBUST_PRIO_PROTECT), long(_POSIX_THREAD_ROBUST_PRIO_PROTECT));
#endif
#if defined(_SC_BARRIERS) && defined(_POSIX_BARRIERS)
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_BARRIERS), long(_POSIX_BARRIERS));
#endif
#if defined(_SC_READER_WRITER_LOCKS) && defined(_POSIX_READER_WRITER_LOCKS)
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_READER_WRITER_LOCKS), long(_POSIX_READER_WRITER_LOCKS));
#endif
#if defined(_SC_SPIN_LOCKS) && defined(_POSIX_SPIN_LOCKS)
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_SPIN_LOCKS), long(_POSIX_SPIN_LOCKS));
#endif
}

TEST(LlvmLibcSysconfTest, ArgMaxTest) {
  long arg_max = LIBC_NAMESPACE::sysconf(_SC_ARG_MAX);
  ASSERT_GT(arg_max, 0L);
  ASSERT_GE(arg_max, 131072L);
}

TEST(LlvmLibcSysconfTest, OpenMaxTest) {
  long open_max = LIBC_NAMESPACE::sysconf(_SC_OPEN_MAX);
  if (open_max == -1)
    return;
  ASSERT_GT(open_max, 0L);
}

TEST(LlvmLibcSysconfTest, PhysPagesTest) {
  long phys_pages = LIBC_NAMESPACE::sysconf(_SC_PHYS_PAGES);
  ASSERT_GT(phys_pages, 0L);
}

TEST(LlvmLibcSysconfTest, ClkTckTest) {
  long clk_tck = LIBC_NAMESPACE::sysconf(_SC_CLK_TCK);
  ASSERT_GT(clk_tck, 0L);
}
