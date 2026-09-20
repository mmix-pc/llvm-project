//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: no-localization

#include <__locale_dir/messages.h>

// LLVM libc provides C locales, but not a message-catalog backend.
#if _LIBCPP_LIBC_LLVM_LIBC
static_assert(!_LIBCPP_HAS_CATOPEN, "LLVM libc must use the no-catalog path");
#endif
