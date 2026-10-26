//===-- Declaration of dngettext ----------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LIBINTL_DNGETTEXT_H
#define LLVM_LIBC_SRC_LIBINTL_DNGETTEXT_H

#include "hdr/types/locale_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
char *dngettext(const char *domain, const char *singular, const char *plural,
                unsigned long count);
} // namespace LIBC_NAMESPACE_DECL
#endif
