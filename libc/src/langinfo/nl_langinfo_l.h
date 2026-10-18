//===-- nl_langinfo_l declaration --------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LANGINFO_NL_LANGINFO_L_H
#define LLVM_LIBC_SRC_LANGINFO_NL_LANGINFO_L_H
#include "src/langinfo/langinfo.h"
namespace LIBC_NAMESPACE_DECL {
char *nl_langinfo_l(nl_item item, locale_t loc);
}
#endif
