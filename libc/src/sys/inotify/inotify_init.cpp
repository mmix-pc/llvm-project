// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/sys/inotify/inotify_init.h"
#include "src/sys/inotify/inotify_init1.h"
#include "src/__support/common.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, inotify_init, ()) {
  return LIBC_NAMESPACE::inotify_init1(0);
}
} // namespace LIBC_NAMESPACE_DECL
