//===-- Implementation header for mbrtoc16--------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_UCHAR_MBRTOC16_H
#define LLVM_LIBC_SRC_UCHAR_MBRTOC16_H
#include "hdr/types/char16_t.h"
#include "hdr/types/mbstate_t.h"
#include "hdr/types/size_t.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
size_t mbrtoc16(char16_t *__restrict out, const char *__restrict s, size_t n,
                mbstate_t *__restrict ps);
}
#endif
