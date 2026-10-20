//===-- Group database stream operations -------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_GRP_STREAM_H
#define LLVM_LIBC_SRC_GRP_STREAM_H

#include "src/grp/lookup.h"

namespace LIBC_NAMESPACE_DECL {
namespace group_db {
ErrorOr<File *> enumeration_stream();
int rewind_enumeration();
void close_enumeration();
int read_entry(File *file, struct group *entry, char *buffer, size_t size,
               struct group **result,
               internal::AccountBuffer<struct group> *storage = nullptr);
struct group *read_shared(File *file);
} // namespace group_db
} // namespace LIBC_NAMESPACE_DECL

#endif
