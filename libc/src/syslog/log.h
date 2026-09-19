//===-- Internal local logging interface ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SYSLOG_LOG_H
#define LLVM_LIBC_SRC_SYSLOG_LOG_H

#include "src/__support/macros/config.h"
#include <stdarg.h>

namespace LIBC_NAMESPACE_DECL {
namespace local_log {
void open(const char *ident, int options, int facility);
void close();
int set_mask(int mask);
void write(int priority, const char *format, va_list args);
} // namespace local_log
} // namespace LIBC_NAMESPACE_DECL

#endif
