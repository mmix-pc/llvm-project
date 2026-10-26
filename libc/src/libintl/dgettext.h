//===-- Declaration of dgettext ----------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LIBINTL_DGETTEXT_H
#define LLVM_LIBC_SRC_LIBINTL_DGETTEXT_H

#include "hdr/types/locale_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
char *dgettext(const char *domain, const char *singular);
} // namespace LIBC_NAMESPACE_DECL
#endif
