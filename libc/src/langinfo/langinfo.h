//===-- Built-in locale information -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LANGINFO_LANGINFO_H
#define LLVM_LIBC_SRC_LANGINFO_LANGINFO_H
#include "hdr/types/locale_t.h"
#include "hdr/types/nl_item.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
namespace internal {
char *langinfo(nl_item item, locale_t loc);
}
} // namespace LIBC_NAMESPACE_DECL
#endif
