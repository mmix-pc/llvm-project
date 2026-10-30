//===-- Linux implementation of semctl ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/sem/semctl.h"

#include "hdr/errno_macros.h"
#include "hdr/sys_ipc_macros.h"
#include "hdr/sys_sem_macros.h"
#include "hdr/types/struct_semid_ds.h"
#include "hdr/types/struct_seminfo.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include <stdarg.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

namespace {

// Applications supply this union by value, not the selected scalar member.
union semun {
  int val;
  struct semid_ds *buf;
  unsigned short *array;
  struct seminfo *__buf;
};

} // namespace

LLVM_LIBC_FUNCTION(int, semctl, (int semid, int semnum, int cmd, ...)) {
  unsigned long cmd_arg = 0;

  switch (cmd) {
    // These commands do not require a fourth argument.
  case IPC_RMID:
  case GETVAL:
  case GETPID:
  case GETNCNT:
  case GETZCNT:
    break;

  case SETVAL: {
    va_list vargs;
    va_start(vargs, cmd);
    int value = va_arg(vargs, union semun).val;
    va_end(vargs);
    // Linux expects the int at the start of a native union-sized word:
    // high 32 bits on big-endian LP64, low 32 bits otherwise. Copy only the
    // initialized member into a zeroed word, never the union's padding.
    static_assert(sizeof(cmd_arg) >= sizeof(value));
    __builtin_memcpy(&cmd_arg, &value, sizeof(value));
    break;
  }

  case IPC_SET:
  case IPC_STAT:
  case SEM_STAT:
  case SEM_STAT_ANY: {
    va_list vargs;
    va_start(vargs, cmd);
    cmd_arg = reinterpret_cast<unsigned long>(va_arg(vargs, union semun).buf);
    va_end(vargs);
    break;
  }

  case GETALL:
  case SETALL: {
    va_list vargs;
    va_start(vargs, cmd);
    cmd_arg = reinterpret_cast<unsigned long>(va_arg(vargs, union semun).array);
    va_end(vargs);
    break;
  }

  case IPC_INFO:
  case SEM_INFO: {
    va_list vargs;
    va_start(vargs, cmd);
    cmd_arg = reinterpret_cast<unsigned long>(va_arg(vargs, union semun).__buf);
    va_end(vargs);
    break;
  }

  default:
    libc_errno = EINVAL;
    return -1;
  }

  int ret =
      LIBC_NAMESPACE::syscall_impl<int>(SYS_semctl, semid, semnum, cmd, cmd_arg);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return ret;
}

} // namespace LIBC_NAMESPACE_DECL
