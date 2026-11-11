//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
#include "hdr/errno_macros.h"
#include "hdr/stdint_proxy.h"
#include "src/pthread/pthread_attr_setstack.h"
#include "src/pthread/pthread_attr_setstacksize.h"
#include <pthread.h>

static bool unchanged(const pthread_attr_t &a, const pthread_attr_t &b) {
  return a.__stack == b.__stack && a.__stacksize == b.__stacksize &&
         a.__guardsize == b.__guardsize && a.__detachstate == b.__detachstate;
}

int main() {
  constexpr size_t limit = size_t{1} << 30;
  constexpr uintptr_t end = uintptr_t{1} << 43;
  pthread_attr_t attr{};
  // Arithmetic-only target addresses: no host memory is read or transferred.
  void *base = reinterpret_cast<void *>(uintptr_t{0x100000});
  attr.__stack = base;
  attr.__stacksize = 65536;
  attr.__guardsize = 19;
  attr.__detachstate = PTHREAD_CREATE_DETACHED;
  const size_t valid[] = {PTHREAD_STACK_MIN, PTHREAD_STACK_MIN + 1, limit - 1,
                          limit};
  for (size_t size : valid) {
    if (LIBC_NAMESPACE::pthread_attr_setstacksize(&attr, size) ||
        attr.__stacksize != size || attr.__stack != base ||
        attr.__guardsize != 19 || attr.__detachstate != PTHREAD_CREATE_DETACHED)
      return 1;
  }
  const size_t invalid[] = {0, PTHREAD_STACK_MIN - 1, limit + 1, SIZE_MAX};
  for (size_t size : invalid) {
    const pthread_attr_t before = attr;
    if (LIBC_NAMESPACE::pthread_attr_setstacksize(&attr, size) != EINVAL ||
        !unchanged(attr, before))
      return 2;
    if (LIBC_NAMESPACE::pthread_attr_setstack(&attr, base, size) != EINVAL ||
        !unchanged(attr, before))
      return 3;
  }
  const uintptr_t bad_base[] = {0x100001, end - 8, end, UINTPTR_MAX - 7};
  for (uintptr_t address : bad_base) {
    const pthread_attr_t before = attr;
    if (LIBC_NAMESPACE::pthread_attr_setstack(
            &attr, reinterpret_cast<void *>(address), PTHREAD_STACK_MIN) !=
            EINVAL ||
        !unchanged(attr, before))
      return 4;
  }
  const pthread_attr_t before = attr;
  if (LIBC_NAMESPACE::pthread_attr_setstack(&attr, base, limit - 1) != EINVAL ||
      !unchanged(attr, before))
    return 5;
  const uintptr_t valid_base[] = {0x100000, end - limit};
  for (uintptr_t address : valid_base) {
    if (LIBC_NAMESPACE::pthread_attr_setstack(
            &attr, reinterpret_cast<void *>(address), limit) ||
        attr.__stack != reinterpret_cast<void *>(address) ||
        attr.__stacksize != limit || attr.__guardsize != 19 ||
        attr.__detachstate != PTHREAD_CREATE_DETACHED)
      return 6;
  }
  return 0;
}
