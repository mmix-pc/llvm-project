//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
#include "config/app.h"
#include "hdr/errno_macros.h"
#include "hdr/stdint_proxy.h"
#include "src/pthread/pthread_attr_setguardsize.h"
#include <pthread.h>

namespace LIBC_NAMESPACE_DECL {
AppProperties app{};
}

int main() {
  pthread_attr_t attr{};
  unsigned char storage[64];
  attr.__stack = storage;
  attr.__stacksize = 123456;
  attr.__detachstate = PTHREAD_CREATE_DETACHED;
  const size_t pages[] = {4096, 8192, 65536};
  for (size_t page : pages) {
    LIBC_NAMESPACE::app.page_size = page;
    const size_t last = SIZE_MAX - SIZE_MAX % page;
    const size_t valid[] = {0, 1, page - 1, page, page + 1, last - 1, last};
    for (size_t value : valid) {
      if (LIBC_NAMESPACE::pthread_attr_setguardsize(&attr, value) != 0 ||
          attr.__guardsize != value)
        return 1;
    }
    const size_t invalid[] = {last + 1, SIZE_MAX};
    for (size_t value : invalid) {
      attr.__guardsize = 17;
      if (LIBC_NAMESPACE::pthread_attr_setguardsize(&attr, value) != EINVAL ||
          attr.__guardsize != 17)
        return 2;
    }
    if (attr.__stack != storage || attr.__stacksize != 123456 ||
        attr.__detachstate != PTHREAD_CREATE_DETACHED)
      return 3;
  }
  LIBC_NAMESPACE::app.page_size = 0;
  if (LIBC_NAMESPACE::pthread_attr_setguardsize(&attr, 1) != EINVAL ||
      attr.__guardsize != 17)
    return 4;
  return 0;
}
