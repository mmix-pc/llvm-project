//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/net/getifaddrs.h"
#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "src/__support/OSUtil/linux/network_syscall_policy.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/net/linux/ifaddrs_impl.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
struct AddressPolicy : net::DefaultNetworkSyscallPolicy {
  static void *allocate(size_t size) { return ::malloc(size); }
  static void deallocate(void *pointer) { ::free(pointer); }
};
} // namespace

LLVM_LIBC_FUNCTION(int, getifaddrs, (ifaddrs * *list)) {
  if (!list) {
    libc_errno = EFAULT;
    return -1;
  }
  *list = nullptr;
  int saved_errno = libc_errno;
  auto result = net::ifaddrs_detail::enumerate<AddressPolicy>();
  if (!result.has_value()) {
    libc_errno = result.error();
    return -1;
  }
  *list = *result;
  libc_errno = saved_errno;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
