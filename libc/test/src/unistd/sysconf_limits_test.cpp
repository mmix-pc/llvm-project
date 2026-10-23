//===-- Tests for Linux sysconf limits and errors -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/limits_macros.h"
#include "hdr/pthread_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/stdio_macros.h"
#include "hdr/sys_resource_macros.h"
#include "hdr/unistd_macros.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/attributes.h"
#include "src/unistd/sysconf.h"
#include "sysconf_test_syscall.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
uint64_t limit_value;
int query_error, last_resource;
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace linux_syscalls {

ErrorOr<int> sysconf_test_prlimit(long number, pid_t pid, int resource,
                                  const struct rlimit *new_limit,
                                  struct rlimit *old_limit) {
  if (number != SYS_prlimit64 || pid != 0 || new_limit || !old_limit)
    return Error(EINVAL);
  last_resource = resource;
  if (query_error)
    return Error(query_error);
  // prlimit64 writes two 64-bit fields even in a 32-bit overlay build.
  uint64_t limits[] = {limit_value, limit_value};
  __builtin_memcpy(old_limit, limits, sizeof(limits));
  return 0;
}

ErrorOr<int> sysconf_test_sysinfo(long number, struct sysinfo *info) {
  if (number != SYS_sysinfo || !info)
    return Error(EINVAL);
  return Error(query_error ? query_error : EIO);
}

} // namespace linux_syscalls
} // namespace LIBC_NAMESPACE_DECL

TEST(LlvmLibcSysconfLimitsTest, StableLimitsDoNotQueryResources) {
  query_error = 0;
  const uint64_t limits[] = {0,                512 * 1024,     4 * 1024 * 1024,
                            64 * 1024 * 1024, UINT64_MAX - 1, UINT64_MAX};
  const int errors[] = {0, EACCES, EIO};
  for (int error : errors) {
    query_error = error;
    for (uint64_t limit : limits) {
      limit_value = limit;
      last_resource = -1;
      libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_ARG_MAX), 131072L);
      EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_STREAM_MAX), -1L);
      EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_CHILD_MAX), -1L);
      EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_SIGQUEUE_MAX), -1L);
      EXPECT_EQ(last_resource, -1);
      EXPECT_EQ(int(libc_errno), EDOM);
    }
  }
  query_error = 0;
}

TEST(LlvmLibcSysconfLimitsTest, PositiveErrno) {
  const int errors[] = {EACCES, EIO};
  const int names[] = {_SC_OPEN_MAX, _SC_PHYS_PAGES, _SC_AVPHYS_PAGES};
  for (int error : errors) {
    query_error = error;
    for (int name : names) {
      libc_errno = 0;
      EXPECT_EQ(LIBC_NAMESPACE::sysconf(name), -1L);
      EXPECT_EQ(int(libc_errno), error);
    }
  }
  query_error = 0;
}

TEST(LlvmLibcSysconfLimitsTest, IndeterminateStreamLimit) {
  query_error = EIO;
  const int errors[] = {0, EDOM};
  for (int saved_errno : errors) {
    last_resource = -1;
    libc_errno = saved_errno;
    EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_STREAM_MAX), -1L);
    EXPECT_EQ(int(libc_errno), saved_errno);
    EXPECT_EQ(last_resource, -1);
  }
  query_error = 0;
}

TEST(LlvmLibcSysconfLimitsTest, OpenLimit) {
  query_error = 0;
  limit_value = 1024;
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_OPEN_MAX), 1024L);
  EXPECT_EQ(last_resource, int(RLIMIT_NOFILE));
  EXPECT_EQ(int(libc_errno), EDOM);
  limit_value = 345;
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_OPEN_MAX), 345L);
  query_error = EACCES;
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_OPEN_MAX), -1L);
  EXPECT_EQ(int(libc_errno), EACCES);
  query_error = 0;
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_OPEN_MAX), 345L);
  EXPECT_EQ(int(libc_errno), EDOM);
  limit_value = UINT64_MAX - 1;
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_OPEN_MAX), LONG_MAX);
  limit_value = UINT64_MAX;
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_OPEN_MAX), -1L);
  EXPECT_EQ(int(libc_errno), EDOM);
}

TEST(LlvmLibcSysconfLimitsTest, PublicThreadCapability) {
  libc_errno = EDOM;
  // Internal mutex selection must not override the public capability.
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_THREADS), long(_POSIX_THREADS));
  EXPECT_EQ(int(libc_errno), EDOM);
}

TEST(LlvmLibcSysconfLimitsTest, IndeterminatePasswdBufferSize) {
  const int errors[] = {0, EDOM};
  for (int saved_errno : errors) {
    libc_errno = saved_errno;
    EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_GETPW_R_SIZE_MAX), -1L);
    EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_GETGR_R_SIZE_MAX), -1L);
    EXPECT_EQ(int(libc_errno), saved_errno);
  }
}

#ifdef _SC_DEVICE_CONTROL
TEST(LlvmLibcSysconfLimitsTest, DeviceControlCapability) {
  const int errors[] = {0, EDOM};
  for (int saved_errno : errors) {
    libc_errno = saved_errno;
#if defined(_POSIX_DEVICE_CONTROL) && _POSIX_DEVICE_CONTROL > 0
    EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_DEVICE_CONTROL),
              long(_POSIX_DEVICE_CONTROL));
#else
    EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_DEVICE_CONTROL), -1L);
#endif
    EXPECT_EQ(int(libc_errno), saved_errno);
  }
}
#endif

TEST(LlvmLibcSysconfLimitsTest, PublicLimits) {
  libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_NGROUPS_MAX), long(NGROUPS_MAX));
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_IOV_MAX), long(IOV_MAX));
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_HOST_NAME_MAX), long(HOST_NAME_MAX));
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_SEM_VALUE_MAX), long(SEM_VALUE_MAX));
#ifdef _SC_NSIG
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_NSIG), long(NSIG));
#endif
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_THREAD_KEYS_MAX),
            _POSIX_THREADS > 0 ? long(PTHREAD_KEYS_MAX) : -1L);
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(_SC_THREAD_STACK_MIN),
            _POSIX_THREADS > 0 ? long(PTHREAD_STACK_MIN) : -1L);
  EXPECT_EQ(int(libc_errno), EDOM);
}

TEST(LlvmLibcSysconfLimitsTest, RecognizedNamesPreserveErrno) {
  const int names[] = {
#ifdef _SC_V8_LP64_OFF64
      _SC_V8_LP64_OFF64,
      _SC_V8_ILP32_OFF32,
      _SC_V8_ILP32_OFFBIG,
      _SC_V8_LPBIG_OFFBIG,
      _SC_XOPEN_UUCP,
#endif
      _SC_NGROUPS_MAX,
      _SC_TZNAME_MAX,
      _SC_JOB_CONTROL,
      _SC_SAVED_IDS,
      _SC_REALTIME_SIGNALS,
      _SC_PRIORITY_SCHEDULING,
      _SC_TIMERS,
      _SC_ASYNCHRONOUS_IO,
      _SC_PRIORITIZED_IO,
      _SC_SYNCHRONIZED_IO,
      _SC_FSYNC,
      _SC_MAPPED_FILES,
      _SC_MEMLOCK,
      _SC_MEMLOCK_RANGE,
      _SC_MEMORY_PROTECTION,
      _SC_MESSAGE_PASSING,
      _SC_SEMAPHORES,
      _SC_SHARED_MEMORY_OBJECTS,
      _SC_AIO_LISTIO_MAX,
      _SC_AIO_MAX,
      _SC_AIO_PRIO_DELTA_MAX,
      _SC_DELAYTIMER_MAX,
      _SC_MQ_OPEN_MAX,
      _SC_MQ_PRIO_MAX,
      _SC_VERSION,
      _SC_RTSIG_MAX,
      _SC_SEM_NSEMS_MAX,
      _SC_SEM_VALUE_MAX,
      _SC_TIMER_MAX,
      _SC_BC_BASE_MAX,
      _SC_BC_DIM_MAX,
      _SC_BC_SCALE_MAX,
      _SC_BC_STRING_MAX,
      _SC_COLL_WEIGHTS_MAX,
      _SC_EXPR_NEST_MAX,
      _SC_LINE_MAX,
      _SC_RE_DUP_MAX,
      _SC_CHARCLASS_NAME_MAX,
      _SC_2_VERSION,
      _SC_2_C_BIND,
      _SC_2_C_DEV,
      _SC_2_FORT_DEV,
      _SC_2_FORT_RUN,
      _SC_2_SW_DEV,
      _SC_2_LOCALEDEF,
      _SC_IOV_MAX,
      _SC_THREAD_SAFE_FUNCTIONS,
      _SC_GETGR_R_SIZE_MAX,
      _SC_LOGIN_NAME_MAX,
      _SC_TTY_NAME_MAX,
      _SC_THREAD_DESTRUCTOR_ITERATIONS,
      _SC_THREAD_KEYS_MAX,
      _SC_THREAD_STACK_MIN,
      _SC_THREAD_THREADS_MAX,
      _SC_THREAD_ATTR_STACKADDR,
      _SC_THREAD_ATTR_STACKSIZE,
      _SC_THREAD_PRIORITY_SCHEDULING,
      _SC_THREAD_PRIO_INHERIT,
      _SC_THREAD_PRIO_PROTECT,
      _SC_THREAD_PROCESS_SHARED,
      _SC_ATEXIT_MAX,
      _SC_XOPEN_VERSION,
      _SC_XOPEN_XCU_VERSION,
      _SC_XOPEN_UNIX,
      _SC_XOPEN_CRYPT,
      _SC_XOPEN_ENH_I18N,
      _SC_XOPEN_SHM,
      _SC_2_CHAR_TERM,
      _SC_2_C_VERSION,
      _SC_2_UPE,
      _SC_XBS5_ILP32_OFF32,
      _SC_XBS5_ILP32_OFFBIG,
      _SC_XBS5_LP64_OFF64,
      _SC_XBS5_LPBIG_OFFBIG,
      _SC_XOPEN_LEGACY,
      _SC_XOPEN_REALTIME,
      _SC_XOPEN_REALTIME_THREADS,
      _SC_ADVISORY_INFO,
      _SC_BARRIERS,
      _SC_CLOCK_SELECTION,
      _SC_CPUTIME,
      _SC_THREAD_CPUTIME,
      _SC_MONOTONIC_CLOCK,
      _SC_READER_WRITER_LOCKS,
      _SC_SPIN_LOCKS,
      _SC_REGEXP,
      _SC_SHELL,
      _SC_SPAWN,
      _SC_SPORADIC_SERVER,
      _SC_THREAD_SPORADIC_SERVER,
      _SC_TIMEOUTS,
      _SC_TYPED_MEMORY_OBJECTS,
      _SC_2_PBS,
      _SC_2_PBS_ACCOUNTING,
      _SC_2_PBS_LOCATE,
      _SC_2_PBS_MESSAGE,
      _SC_2_PBS_TRACK,
      _SC_SYMLOOP_MAX,
      _SC_STREAMS,
      _SC_2_PBS_CHECKPOINT,
      _SC_V6_ILP32_OFF32,
      _SC_V6_ILP32_OFFBIG,
      _SC_V6_LP64_OFF64,
      _SC_V6_LPBIG_OFFBIG,
      _SC_HOST_NAME_MAX,
      _SC_TRACE,
      _SC_TRACE_EVENT_FILTER,
      _SC_TRACE_INHERIT,
      _SC_TRACE_LOG,
      _SC_IPV6,
      _SC_RAW_SOCKETS,
      _SC_V7_ILP32_OFF32,
      _SC_V7_ILP32_OFFBIG,
      _SC_V7_LP64_OFF64,
      _SC_V7_LPBIG_OFFBIG,
      _SC_SS_REPL_MAX,
      _SC_TRACE_EVENT_NAME_MAX,
      _SC_TRACE_NAME_MAX,
      _SC_TRACE_SYS_MAX,
      _SC_TRACE_USER_EVENT_MAX,
      _SC_XOPEN_STREAMS,
      _SC_THREAD_ROBUST_PRIO_INHERIT,
      _SC_THREAD_ROBUST_PRIO_PROTECT,
  };
  for (int name : names) {
    libc_errno = EDOM;
    (void)LIBC_NAMESPACE::sysconf(name);
    EXPECT_EQ(int(libc_errno), EDOM);
  }
}

TEST(LlvmLibcSysconfLimitsTest, InvalidName) {

  libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::sysconf(-1), -1L);
  EXPECT_EQ(int(libc_errno), EINVAL);
}
