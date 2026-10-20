//===-- Accounting engine --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_UTMP_UTMP_ENGINE_H
#define LLVM_LIBC_SRC_UTMP_UTMP_ENGINE_H

#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace utmp_backend {

enum class Family { UTMP, UTMPX };
enum class Search { NEXT, ID, LINE };

// Both API families share the pathname, descriptor and cursor. Returned
// storage has the requested C type; records are transferred as bytes, never
// accessed through a pointer to the other struct type.
void rewind();
void end();
int name(const char *path);
void *get(Family family, Search search, const void *key = nullptr);
void *put(Family family, const void *record);
void append(const char *path, const void *record);

} // namespace utmp_backend
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_UTMP_UTMP_ENGINE_H
