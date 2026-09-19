//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SYSLOG_OPENLOG_H
#define LLVM_LIBC_SRC_SYSLOG_OPENLOG_H

#include "src/__support/macros/config.h"
#include <stdarg.h>

namespace LIBC_NAMESPACE_DECL {
void openlog(const char *ident, int options, int facility);
} // namespace LIBC_NAMESPACE_DECL

#endif
