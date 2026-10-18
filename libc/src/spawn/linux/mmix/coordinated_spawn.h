//===-- Coordinated MMIX spawn snapshot ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_SPAWN_LINUX_MMIX_COORDINATED_SPAWN_H
#define LLVM_LIBC_SRC_SPAWN_LINUX_MMIX_COORDINATED_SPAWN_H
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
// Caller blocks signals before entry and keeps them blocked through child
// disposition setup. Returns a PID, zero in the child, or a negative errno.
long coordinated_spawn_clone();
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
