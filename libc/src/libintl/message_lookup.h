//===-- Public message lookup implementation --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LIBINTL_MESSAGE_LOOKUP_H
#define LLVM_LIBC_SRC_LIBINTL_MESSAGE_LOOKUP_H

#include "hdr/types/locale_t.h"
#include "src/__support/CPP/optional.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// A null locale selects the calling thread's locale. Explicit objects remain
// borrowed for the call; no thread locale selection is changed.
char *lookup_message(const char *domain, const char *singular,
                     const char *plural, cpp::optional<unsigned long> count,
                     int category, locale_t locale);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
