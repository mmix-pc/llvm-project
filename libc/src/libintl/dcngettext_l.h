//===-- Declaration of dcngettext_l ----------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LIBINTL_DCNGETTEXT_L_H
#define LLVM_LIBC_SRC_LIBINTL_DCNGETTEXT_L_H

#include "hdr/types/locale_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
char *dcngettext_l(const char *domain, const char *singular, const char *plural,
                   unsigned long count, int category, locale_t locale);
} // namespace LIBC_NAMESPACE_DECL
#endif
