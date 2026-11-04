//===-- Linux configuration limit dispatch ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_UNISTD_LINUX_SYSCONF_LIMITS_H
#define LLVM_LIBC_SRC_UNISTD_LINUX_SYSCONF_LIMITS_H

#include "src/__support/macros/config.h"
#include "src/__support/macros/properties/architectures.h"

#ifdef LIBC_TARGET_ARCH_IS_MMIX
#include "src/unistd/linux/mmix/sysconf_limits.h"
#else
namespace LIBC_NAMESPACE_DECL {

constexpr long tty_name_max() { return -1; }

} // namespace LIBC_NAMESPACE_DECL
#endif

#endif // LLVM_LIBC_SRC_UNISTD_LINUX_SYSCONF_LIMITS_H
