//===-- Implementation of alphasort ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/dirent/alphasort.h"
#include "src/__support/common.h"
#include "src/string/strcoll.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, alphasort,
                   (const struct dirent **left, const struct dirent **right)) {
  return LIBC_NAMESPACE::strcoll((*left)->d_name, (*right)->d_name);
}
} // namespace LIBC_NAMESPACE_DECL
