//===-- Implementation of fcntl -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/fcntl/fcntl.h"

#include "hdr/stdint_proxy.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/fcntl.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"

#include <stdarg.h>

namespace LIBC_NAMESPACE_DECL {
namespace {

// Architecture-independent Linux UAPI extension command numbers. Do not use
// host-header availability to decide whether to consume a variadic argument.
enum LinuxFcntlCommand {
  SetLease = 1024,
  GetLease = 1025,
  Notify = 1026,
  DupfdQuery = 1027,
  CreatedQuery = 1028,
  DupfdCloexec = 1030,
  SetPipeSize = 1031,
  GetPipeSize = 1032,
  AddSeals = 1033,
  GetSeals = 1034,
};

} // namespace

LLVM_LIBC_FUNCTION(int, fcntl, (int fd, int cmd, ...)) {
  va_list varargs;
  va_start(varargs, cmd);
  void *arg = nullptr;
  switch (cmd) {
  case F_GETFD:
  case F_GETFL:
  case F_GETOWN:
  case F_GETSIG:
  case GetLease:
  case GetPipeSize:
  case GetSeals:
  case CreatedQuery:
    break;
  case F_DUPFD:
  case F_SETFD:
  case F_SETFL:
  case F_SETOWN:
  case F_SETSIG:
  case DupfdCloexec:
  case DupfdQuery:
  case SetLease:
  case SetPipeSize:
  case AddSeals:
    arg = reinterpret_cast<void *>(static_cast<intptr_t>(va_arg(varargs, int)));
    break;
  case Notify:
    arg = reinterpret_cast<void *>(va_arg(varargs, long));
    break;
  case F_GETLK:
  case F_SETLK:
  case F_SETLKW:
  case F_OFD_GETLK:
  case F_OFD_SETLK:
  case F_OFD_SETLKW:
    arg = va_arg(varargs, struct flock *);
    break;
#if defined(F_GETLK64) && F_GETLK64 != F_GETLK
  case F_GETLK64:
  case F_SETLK64:
  case F_SETLKW64:
    arg = va_arg(varargs, struct flock64 *);
    break;
#endif
  case F_GETOWN_EX:
  case F_SETOWN_EX:
    arg = va_arg(varargs, struct f_owner_ex *);
    break;
  default:
    // Preserve raw-pointer passthrough for unclassified extensions. Their
    // argument types cannot be inferred here.
    arg = va_arg(varargs, void *);
    break;
  }
  va_end(varargs);

  auto result = LIBC_NAMESPACE::linux_syscalls::fcntl(fd, cmd, arg);

  if (!result.has_value()) {
    libc_errno = result.error();
    return -1;
  }
  return result.value();
}

} // namespace LIBC_NAMESPACE_DECL
