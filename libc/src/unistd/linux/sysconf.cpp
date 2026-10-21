//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Linux implementation of sysconf
///
//===----------------------------------------------------------------------===//

#include "src/unistd/sysconf.h"

#include "src/__support/common.h"

#include "hdr/limits_macros.h"
#include "hdr/pthread_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/stdio_macros.h"
#include "hdr/sys_auxv_macros.h"
#include "hdr/sys_resource_macros.h"
#include "hdr/types/off_t.h"
#include "hdr/types/struct_rlimit.h"
#include "hdr/types/struct_sysinfo.h"
#include "hdr/unistd_macros.h"
#include "src/__support/OSUtil/linux/auxv.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/prlimit.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/sysinfo.h"
#include "src/__support/OSUtil/linux/sysinfo.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include <linux/limits.h>

// In overlay mode, system headers (like glibc's <bits/local_lim.h>) may
// explicitly undefine ARG_MAX. Linux preserves this argument-space floor
// independently of RLIMIT_STACK; report it rather than a changing quota.
#ifndef ARG_MAX
#define ARG_MAX 131072
#endif

namespace LIBC_NAMESPACE_DECL {

namespace { // Anonymous namespace for internal helpers

// We define a local structure for prlimit64 to avoid type mismatches
// and stack corruption on 32-bit systems when in overlay mode.
struct rlimit64 {
  uint64_t rlim_cur;
  uint64_t rlim_max;
};

long get_limit(int resource) {
  struct rlimit64 limits;
  ErrorOr<int> ret = linux_syscalls::prlimit(
      0, resource, nullptr, reinterpret_cast<struct rlimit *>(&limits));
  if (!ret) {
    libc_errno = ret.error();
    return -1;
  }
  if (limits.rlim_cur == ~0ULL)
    return -1;
  if (limits.rlim_cur > static_cast<uint64_t>(LONG_MAX))
    return LONG_MAX;
  return static_cast<long>(limits.rlim_cur);
}

long get_page_size() {
  cpp::optional<unsigned long> page_size = auxv::get(AT_PAGESZ);
  if (page_size)
    return static_cast<long>(*page_size);
  libc_errno = EINVAL;
  return -1;
}

long get_nprocessors_conf() {
  return static_cast<long>(
      sysinfo::parse_nproc_with_fallback_from(sysinfo::POSSIBLE_NPROC_PATH));
}

long get_nprocessors_onln() {
  return static_cast<long>(
      sysinfo::parse_nproc_with_fallback_from(sysinfo::ONLINE_NPROC_PATH));
}

long get_phys_pages(bool available) {
  struct ::sysinfo info;
  ErrorOr<int> ret = linux_syscalls::sysinfo(&info);
  if (!ret) {
    libc_errno = ret.error();
    return -1;
  }
  cpp::optional<unsigned long> page_size = auxv::get(AT_PAGESZ);
  if (!page_size) {
    libc_errno = ENOSYS;
    return -1;
  }
  unsigned long ps = *page_size;
  unsigned long mem_unit = info.mem_unit;
  unsigned long num = available ? info.freeram : info.totalram;
  if (mem_unit >= ps) {
    num *= (mem_unit / ps);
  } else {
    num /= (ps / mem_unit);
  }
  return static_cast<long>(num);
}

} // anonymous namespace

LLVM_LIBC_FUNCTION(long, sysconf, (int name)) {
  switch (name) {
  case _SC_ARG_MAX:
    return ARG_MAX;
  case _SC_CLK_TCK:
    if (auto ticks = auxv::get(AT_CLKTCK))
      return static_cast<long>(*ticks);
    libc_errno = ENOSYS;
    return -1;
  case _SC_PAGESIZE:
    return get_page_size();
  case _SC_NPROCESSORS_CONF:
    return get_nprocessors_conf();
  case _SC_NPROCESSORS_ONLN:
    return get_nprocessors_onln();
  case _SC_THREADS:
    return _POSIX_THREADS;
  case _SC_JOB_CONTROL:
#ifdef _POSIX_JOB_CONTROL
    return _POSIX_JOB_CONTROL > 0 ? _POSIX_JOB_CONTROL : -1;
#else
    return -1;
#endif
  case _SC_SAVED_IDS:
#ifdef _POSIX_SAVED_IDS
    return _POSIX_SAVED_IDS > 0 ? _POSIX_SAVED_IDS : -1;
#else
    return -1;
#endif
  case _SC_REALTIME_SIGNALS:
#ifdef _POSIX_REALTIME_SIGNALS
    return _POSIX_REALTIME_SIGNALS > 0 ? _POSIX_REALTIME_SIGNALS : -1;
#else
    return -1;
#endif
  case _SC_PRIORITY_SCHEDULING:
#ifdef _POSIX_PRIORITY_SCHEDULING
    return _POSIX_PRIORITY_SCHEDULING > 0 ? _POSIX_PRIORITY_SCHEDULING : -1;
#else
    return -1;
#endif
  case _SC_TIMERS:
#ifdef _POSIX_TIMERS
    return _POSIX_TIMERS > 0 ? _POSIX_TIMERS : -1;
#else
    return -1;
#endif
  case _SC_ASYNCHRONOUS_IO:
#ifdef _POSIX_ASYNCHRONOUS_IO
    return _POSIX_ASYNCHRONOUS_IO > 0 ? _POSIX_ASYNCHRONOUS_IO : -1;
#else
    return -1;
#endif
  case _SC_PRIORITIZED_IO:
#ifdef _POSIX_PRIORITIZED_IO
    return _POSIX_PRIORITIZED_IO > 0 ? _POSIX_PRIORITIZED_IO : -1;
#else
    return -1;
#endif
  case _SC_SYNCHRONIZED_IO:
#ifdef _POSIX_SYNCHRONIZED_IO
    return _POSIX_SYNCHRONIZED_IO > 0 ? _POSIX_SYNCHRONIZED_IO : -1;
#else
    return -1;
#endif
  case _SC_FSYNC:
#ifdef _POSIX_FSYNC
    return _POSIX_FSYNC > 0 ? _POSIX_FSYNC : -1;
#else
    return -1;
#endif
  case _SC_MAPPED_FILES:
#ifdef _POSIX_MAPPED_FILES
    return _POSIX_MAPPED_FILES > 0 ? _POSIX_MAPPED_FILES : -1;
#else
    return -1;
#endif
  case _SC_MEMLOCK:
#ifdef _POSIX_MEMLOCK
    return _POSIX_MEMLOCK > 0 ? _POSIX_MEMLOCK : -1;
#else
    return -1;
#endif
  case _SC_MEMLOCK_RANGE:
#ifdef _POSIX_MEMLOCK_RANGE
    return _POSIX_MEMLOCK_RANGE > 0 ? _POSIX_MEMLOCK_RANGE : -1;
#else
    return -1;
#endif
  case _SC_MEMORY_PROTECTION:
#ifdef _POSIX_MEMORY_PROTECTION
    return _POSIX_MEMORY_PROTECTION > 0 ? _POSIX_MEMORY_PROTECTION : -1;
#else
    return -1;
#endif
  case _SC_MESSAGE_PASSING:
#ifdef _POSIX_MESSAGE_PASSING
    return _POSIX_MESSAGE_PASSING > 0 ? _POSIX_MESSAGE_PASSING : -1;
#else
    return -1;
#endif
  case _SC_SEMAPHORES:
#ifdef _POSIX_SEMAPHORES
    return _POSIX_SEMAPHORES > 0 ? _POSIX_SEMAPHORES : -1;
#else
    return -1;
#endif
  case _SC_SHARED_MEMORY_OBJECTS:
#ifdef _POSIX_SHARED_MEMORY_OBJECTS
    return _POSIX_SHARED_MEMORY_OBJECTS > 0 ? _POSIX_SHARED_MEMORY_OBJECTS : -1;
#else
    return -1;
#endif
  case _SC_VERSION:
#ifdef _POSIX_VERSION
    return _POSIX_VERSION > 0 ? _POSIX_VERSION : -1;
#else
    return -1;
#endif
  case _SC_THREAD_SAFE_FUNCTIONS:
#ifdef _POSIX_THREAD_SAFE_FUNCTIONS
    return _POSIX_THREAD_SAFE_FUNCTIONS > 0 ? _POSIX_THREAD_SAFE_FUNCTIONS : -1;
#else
    return -1;
#endif
  case _SC_THREAD_ATTR_STACKADDR:
#ifdef _POSIX_THREAD_ATTR_STACKADDR
    return _POSIX_THREAD_ATTR_STACKADDR > 0 ? _POSIX_THREAD_ATTR_STACKADDR : -1;
#else
    return -1;
#endif
  case _SC_THREAD_ATTR_STACKSIZE:
#ifdef _POSIX_THREAD_ATTR_STACKSIZE
    return _POSIX_THREAD_ATTR_STACKSIZE > 0 ? _POSIX_THREAD_ATTR_STACKSIZE : -1;
#else
    return -1;
#endif
  case _SC_THREAD_PRIORITY_SCHEDULING:
#ifdef _POSIX_THREAD_PRIORITY_SCHEDULING
    return _POSIX_THREAD_PRIORITY_SCHEDULING > 0
               ? _POSIX_THREAD_PRIORITY_SCHEDULING
               : -1;
#else
    return -1;
#endif
  case _SC_THREAD_PRIO_INHERIT:
#ifdef _POSIX_THREAD_PRIO_INHERIT
    return _POSIX_THREAD_PRIO_INHERIT > 0 ? _POSIX_THREAD_PRIO_INHERIT : -1;
#else
    return -1;
#endif
  case _SC_THREAD_PRIO_PROTECT:
#ifdef _POSIX_THREAD_PRIO_PROTECT
    return _POSIX_THREAD_PRIO_PROTECT > 0 ? _POSIX_THREAD_PRIO_PROTECT : -1;
#else
    return -1;
#endif
  case _SC_THREAD_PROCESS_SHARED:
#ifdef _POSIX_THREAD_PROCESS_SHARED
    return _POSIX_THREAD_PROCESS_SHARED > 0 ? _POSIX_THREAD_PROCESS_SHARED : -1;
#else
    return -1;
#endif
  case _SC_THREAD_CPUTIME:
#ifdef _POSIX_THREAD_CPUTIME
    return _POSIX_THREAD_CPUTIME > 0 ? _POSIX_THREAD_CPUTIME : -1;
#else
    return -1;
#endif
  case _SC_THREAD_ROBUST_PRIO_INHERIT:
#ifdef _POSIX_THREAD_ROBUST_PRIO_INHERIT
    return _POSIX_THREAD_ROBUST_PRIO_INHERIT > 0
               ? _POSIX_THREAD_ROBUST_PRIO_INHERIT
               : -1;
#else
    return -1;
#endif
  case _SC_THREAD_ROBUST_PRIO_PROTECT:
#ifdef _POSIX_THREAD_ROBUST_PRIO_PROTECT
    return _POSIX_THREAD_ROBUST_PRIO_PROTECT > 0
               ? _POSIX_THREAD_ROBUST_PRIO_PROTECT
               : -1;
#else
    return -1;
#endif
  case _SC_BARRIERS:
#ifdef _POSIX_BARRIERS
    return _POSIX_BARRIERS > 0 ? _POSIX_BARRIERS : -1;
#else
    return -1;
#endif
  case _SC_READER_WRITER_LOCKS:
#ifdef _POSIX_READER_WRITER_LOCKS
    return _POSIX_READER_WRITER_LOCKS > 0 ? _POSIX_READER_WRITER_LOCKS : -1;
#else
    return -1;
#endif
  case _SC_SPIN_LOCKS:
#ifdef _POSIX_SPIN_LOCKS
    return _POSIX_SPIN_LOCKS > 0 ? _POSIX_SPIN_LOCKS : -1;
#else
    return -1;
#endif
  case _SC_ADVISORY_INFO:
#ifdef _POSIX_ADVISORY_INFO
    return _POSIX_ADVISORY_INFO > 0 ? _POSIX_ADVISORY_INFO : -1;
#else
    return -1;
#endif
  case _SC_CLOCK_SELECTION:
#ifdef _POSIX_CLOCK_SELECTION
    return _POSIX_CLOCK_SELECTION > 0 ? _POSIX_CLOCK_SELECTION : -1;
#else
    return -1;
#endif
  case _SC_CPUTIME:
#ifdef _POSIX_CPUTIME
    return _POSIX_CPUTIME > 0 ? _POSIX_CPUTIME : -1;
#else
    return -1;
#endif
  case _SC_MONOTONIC_CLOCK:
#ifdef _POSIX_MONOTONIC_CLOCK
    return _POSIX_MONOTONIC_CLOCK > 0 ? _POSIX_MONOTONIC_CLOCK : -1;
#else
    return -1;
#endif
  case _SC_REGEXP:
#ifdef _POSIX_REGEXP
    return _POSIX_REGEXP > 0 ? _POSIX_REGEXP : -1;
#else
    return -1;
#endif
  case _SC_SHELL:
#ifdef _POSIX_SHELL
    return _POSIX_SHELL > 0 ? _POSIX_SHELL : -1;
#else
    return -1;
#endif
  case _SC_SPAWN:
#ifdef _POSIX_SPAWN
    return _POSIX_SPAWN > 0 ? _POSIX_SPAWN : -1;
#else
    return -1;
#endif
  case _SC_SPORADIC_SERVER:
#ifdef _POSIX_SPORADIC_SERVER
    return _POSIX_SPORADIC_SERVER > 0 ? _POSIX_SPORADIC_SERVER : -1;
#else
    return -1;
#endif
  case _SC_THREAD_SPORADIC_SERVER:
#ifdef _POSIX_THREAD_SPORADIC_SERVER
    return _POSIX_THREAD_SPORADIC_SERVER > 0 ? _POSIX_THREAD_SPORADIC_SERVER
                                             : -1;
#else
    return -1;
#endif
  case _SC_TIMEOUTS:
#ifdef _POSIX_TIMEOUTS
    return _POSIX_TIMEOUTS > 0 ? _POSIX_TIMEOUTS : -1;
#else
    return -1;
#endif
  case _SC_TYPED_MEMORY_OBJECTS:
#ifdef _POSIX_TYPED_MEMORY_OBJECTS
    return _POSIX_TYPED_MEMORY_OBJECTS > 0 ? _POSIX_TYPED_MEMORY_OBJECTS : -1;
#else
    return -1;
#endif
  case _SC_TRACE:
#ifdef _POSIX_TRACE
    return _POSIX_TRACE > 0 ? _POSIX_TRACE : -1;
#else
    return -1;
#endif
  case _SC_TRACE_EVENT_FILTER:
#ifdef _POSIX_TRACE_EVENT_FILTER
    return _POSIX_TRACE_EVENT_FILTER > 0 ? _POSIX_TRACE_EVENT_FILTER : -1;
#else
    return -1;
#endif
  case _SC_TRACE_INHERIT:
#ifdef _POSIX_TRACE_INHERIT
    return _POSIX_TRACE_INHERIT > 0 ? _POSIX_TRACE_INHERIT : -1;
#else
    return -1;
#endif
  case _SC_TRACE_LOG:
#ifdef _POSIX_TRACE_LOG
    return _POSIX_TRACE_LOG > 0 ? _POSIX_TRACE_LOG : -1;
#else
    return -1;
#endif
  case _SC_IPV6:
#ifdef _POSIX_IPV6
    return _POSIX_IPV6 > 0 ? _POSIX_IPV6 : -1;
#else
    return -1;
#endif
  case _SC_RAW_SOCKETS:
#ifdef _POSIX_RAW_SOCKETS
    return _POSIX_RAW_SOCKETS > 0 ? _POSIX_RAW_SOCKETS : -1;
#else
    return -1;
#endif
  case _SC_2_VERSION:
#ifdef _POSIX2_VERSION
    return _POSIX2_VERSION > 0 ? _POSIX2_VERSION : -1;
#else
    return -1;
#endif
  case _SC_2_C_BIND:
#ifdef _POSIX2_C_BIND
    return _POSIX2_C_BIND > 0 ? _POSIX2_C_BIND : -1;
#else
    return -1;
#endif
  case _SC_2_C_DEV:
#ifdef _POSIX2_C_DEV
    return _POSIX2_C_DEV > 0 ? _POSIX2_C_DEV : -1;
#else
    return -1;
#endif
  case _SC_2_FORT_DEV:
#ifdef _POSIX2_FORT_DEV
    return _POSIX2_FORT_DEV > 0 ? _POSIX2_FORT_DEV : -1;
#else
    return -1;
#endif
  case _SC_2_FORT_RUN:
#ifdef _POSIX2_FORT_RUN
    return _POSIX2_FORT_RUN > 0 ? _POSIX2_FORT_RUN : -1;
#else
    return -1;
#endif
  case _SC_2_SW_DEV:
#ifdef _POSIX2_SW_DEV
    return _POSIX2_SW_DEV > 0 ? _POSIX2_SW_DEV : -1;
#else
    return -1;
#endif
  case _SC_2_LOCALEDEF:
#ifdef _POSIX2_LOCALEDEF
    return _POSIX2_LOCALEDEF > 0 ? _POSIX2_LOCALEDEF : -1;
#else
    return -1;
#endif
  case _SC_2_CHAR_TERM:
#ifdef _POSIX2_CHAR_TERM
    return _POSIX2_CHAR_TERM > 0 ? _POSIX2_CHAR_TERM : -1;
#else
    return -1;
#endif
  case _SC_2_C_VERSION:
#ifdef _POSIX2_C_VERSION
    return _POSIX2_C_VERSION > 0 ? _POSIX2_C_VERSION : -1;
#else
    return -1;
#endif
  case _SC_2_UPE:
#ifdef _POSIX2_UPE
    return _POSIX2_UPE > 0 ? _POSIX2_UPE : -1;
#else
    return -1;
#endif
  case _SC_2_PBS:
#ifdef _POSIX2_PBS
    return _POSIX2_PBS > 0 ? _POSIX2_PBS : -1;
#else
    return -1;
#endif
  case _SC_2_PBS_ACCOUNTING:
#ifdef _POSIX2_PBS_ACCOUNTING
    return _POSIX2_PBS_ACCOUNTING > 0 ? _POSIX2_PBS_ACCOUNTING : -1;
#else
    return -1;
#endif
  case _SC_2_PBS_LOCATE:
#ifdef _POSIX2_PBS_LOCATE
    return _POSIX2_PBS_LOCATE > 0 ? _POSIX2_PBS_LOCATE : -1;
#else
    return -1;
#endif
  case _SC_2_PBS_MESSAGE:
#ifdef _POSIX2_PBS_MESSAGE
    return _POSIX2_PBS_MESSAGE > 0 ? _POSIX2_PBS_MESSAGE : -1;
#else
    return -1;
#endif
  case _SC_2_PBS_TRACK:
#ifdef _POSIX2_PBS_TRACK
    return _POSIX2_PBS_TRACK > 0 ? _POSIX2_PBS_TRACK : -1;
#else
    return -1;
#endif
  case _SC_2_PBS_CHECKPOINT:
#ifdef _POSIX2_PBS_CHECKPOINT
    return _POSIX2_PBS_CHECKPOINT > 0 ? _POSIX2_PBS_CHECKPOINT : -1;
#else
    return -1;
#endif
  case _SC_XOPEN_VERSION:
#ifdef _XOPEN_VERSION
    return _XOPEN_VERSION > 0 ? _XOPEN_VERSION : -1;
#else
    return -1;
#endif
  case _SC_XOPEN_XCU_VERSION:
#ifdef _XOPEN_XCU_VERSION
    return _XOPEN_XCU_VERSION > 0 ? _XOPEN_XCU_VERSION : -1;
#else
    return -1;
#endif
  case _SC_XOPEN_UNIX:
#ifdef _XOPEN_UNIX
    return _XOPEN_UNIX > 0 ? _XOPEN_UNIX : -1;
#else
    return -1;
#endif
  case _SC_XOPEN_CRYPT:
#ifdef _XOPEN_CRYPT
    return _XOPEN_CRYPT > 0 ? _XOPEN_CRYPT : -1;
#else
    return -1;
#endif
  case _SC_XOPEN_ENH_I18N:
#ifdef _XOPEN_ENH_I18N
    return _XOPEN_ENH_I18N > 0 ? _XOPEN_ENH_I18N : -1;
#else
    return -1;
#endif
  case _SC_XOPEN_SHM:
#ifdef _XOPEN_SHM
    return _XOPEN_SHM > 0 ? _XOPEN_SHM : -1;
#else
    return -1;
#endif
  case _SC_XOPEN_LEGACY:
#ifdef _XOPEN_LEGACY
    return _XOPEN_LEGACY > 0 ? _XOPEN_LEGACY : -1;
#else
    return -1;
#endif
  case _SC_XOPEN_REALTIME:
#ifdef _XOPEN_REALTIME
    return _XOPEN_REALTIME > 0 ? _XOPEN_REALTIME : -1;
#else
    return -1;
#endif
  case _SC_XOPEN_REALTIME_THREADS:
#ifdef _XOPEN_REALTIME_THREADS
    return _XOPEN_REALTIME_THREADS > 0 ? _XOPEN_REALTIME_THREADS : -1;
#else
    return -1;
#endif
  case _SC_XOPEN_STREAMS:
#ifdef _XOPEN_STREAMS
    return _XOPEN_STREAMS > 0 ? _XOPEN_STREAMS : -1;
#else
    return -1;
#endif
  case _SC_STREAMS:
#ifdef _XOPEN_STREAMS
    return _XOPEN_STREAMS > 0 ? _XOPEN_STREAMS : -1;
#else
    return -1;
#endif
  case _SC_GETPW_R_SIZE_MAX:
  case _SC_GETGR_R_SIZE_MAX:
    // Database records have no fixed size limit. Preserve errno for this
    // indeterminate value, unlike an invalid configuration name.
    return -1;
  case _SC_OPEN_MAX:
    return get_limit(RLIMIT_NOFILE);
  case _SC_STREAM_MAX:
    // Report the stable stdio guarantee, not the current descriptor quota.
    return FOPEN_MAX;
  case _SC_CHILD_MAX:
  case _SC_SIGQUEUE_MAX:
    // Linux enforces mutable per-user/resource quotas, not a stable limit on
    // these capabilities. An indeterminate limit preserves errno; callers
    // needing their current resource quotas must use getrlimit instead.
    return -1;
  case _SC_PHYS_PAGES:
    return get_phys_pages(false);
  case _SC_AVPHYS_PAGES:
    return get_phys_pages(true);
  case _SC_NGROUPS_MAX:
    return NGROUPS_MAX;
  case _SC_IOV_MAX:
    return IOV_MAX;
  case _SC_BC_BASE_MAX:
    return BC_BASE_MAX;
  case _SC_BC_DIM_MAX:
    return BC_DIM_MAX;
  case _SC_BC_SCALE_MAX:
    return BC_SCALE_MAX;
  case _SC_BC_STRING_MAX:
    return BC_STRING_MAX;
  case _SC_COLL_WEIGHTS_MAX:
    return COLL_WEIGHTS_MAX;
  case _SC_EXPR_NEST_MAX:
    return EXPR_NEST_MAX;
  case _SC_LINE_MAX:
    return LINE_MAX;
  case _SC_RE_DUP_MAX:
    return RE_DUP_MAX;
  case _SC_CHARCLASS_NAME_MAX:
    return CHARCLASS_NAME_MAX;
  case _SC_HOST_NAME_MAX:
    return HOST_NAME_MAX;
  case _SC_SEM_VALUE_MAX:
    return SEM_VALUE_MAX;
  case _SC_RTSIG_MAX:
    return SIGRTMAX - SIGRTMIN + 1;
  case _SC_THREAD_DESTRUCTOR_ITERATIONS:
    return _POSIX_THREADS > 0 ? PTHREAD_DESTRUCTOR_ITERATIONS : -1;
  case _SC_THREAD_KEYS_MAX:
    return _POSIX_THREADS > 0 ? PTHREAD_KEYS_MAX : -1;
  case _SC_THREAD_STACK_MIN:
    return _POSIX_THREADS > 0 ? PTHREAD_STACK_MIN : -1;
  case _SC_SYMLOOP_MAX:
    return 40; // Linux MAXSYMLINKS.
  case _SC_V6_LP64_OFF64:
  case _SC_V7_LP64_OFF64:
  case _SC_XBS5_LP64_OFF64:
#ifdef _SC_V8_LP64_OFF64
  case _SC_V8_LP64_OFF64:
#endif
    return sizeof(int) == 4 && sizeof(long) == 8 && sizeof(void *) == 8 &&
                   sizeof(off_t) == 8
               ? 1
               : -1;
  case _SC_TZNAME_MAX:
  case _SC_LOGIN_NAME_MAX:
  case _SC_TTY_NAME_MAX:
  case _SC_ATEXIT_MAX:
  case _SC_THREAD_THREADS_MAX:
  case _SC_SEM_NSEMS_MAX:
  case _SC_TIMER_MAX:
  case _SC_AIO_LISTIO_MAX:
  case _SC_AIO_MAX:
  case _SC_AIO_PRIO_DELTA_MAX:
  case _SC_DELAYTIMER_MAX:
  case _SC_MQ_OPEN_MAX:
  case _SC_MQ_PRIO_MAX:
  case _SC_SS_REPL_MAX:
  case _SC_TRACE_EVENT_NAME_MAX:
  case _SC_TRACE_NAME_MAX:
  case _SC_TRACE_SYS_MAX:
  case _SC_TRACE_USER_EVENT_MAX:
  case _SC_V6_ILP32_OFF32:
  case _SC_V6_ILP32_OFFBIG:
  case _SC_V6_LPBIG_OFFBIG:
  case _SC_V7_ILP32_OFF32:
  case _SC_V7_ILP32_OFFBIG:
  case _SC_V7_LPBIG_OFFBIG:
  case _SC_XBS5_ILP32_OFF32:
  case _SC_XBS5_ILP32_OFFBIG:
  case _SC_XBS5_LPBIG_OFFBIG:
#ifdef _SC_XOPEN_UUCP
  case _SC_XOPEN_UUCP:
#endif
#ifdef _SC_V8_ILP32_OFF32
  case _SC_V8_ILP32_OFF32:
  case _SC_V8_ILP32_OFFBIG:
  case _SC_V8_LPBIG_OFFBIG:
#endif
    // Known queries with no fixed limit or no selected implementation/model.
    return -1;
  default:

    libc_errno = EINVAL;
    return -1;
  }
}

} // namespace LIBC_NAMESPACE_DECL
