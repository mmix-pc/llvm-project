//===-- Implementation header for strfmon_l ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_MONETARY_STRFMON_L_H
#define LLVM_LIBC_SRC_MONETARY_STRFMON_L_H
#include "hdr/types/size_t.h"
#include "hdr/types/ssize_t.h"
#include "include/llvm-libc-types/locale_t.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
ssize_t strfmon_l(char *__restrict output, size_t capacity, locale_t locale,
                  const char *__restrict format, ...);
}
#endif
