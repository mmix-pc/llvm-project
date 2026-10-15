//===-- MMIX runtime loading API state ---------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_DLFCN_LINUX_MMIX_LOADING_H
#define LLVM_LIBC_SRC_DLFCN_LINUX_MMIX_LOADING_H
#include "src/__support/OSUtil/linux/loader_services.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
// Adopt before application callbacks/workers, never replace a live service.
bool adopt_loading_services(const loader::LoaderAPI &);
void *loader_open(const char *, int, uintptr_t caller);
void *loader_lookup(void *, const char *, uintptr_t caller);
char *loader_error();
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
