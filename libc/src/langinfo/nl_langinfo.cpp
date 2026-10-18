//===-- nl_langinfo implementation ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/langinfo/nl_langinfo.h"
#include "src/__support/common.h"
#include "src/locale/locale.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, nl_langinfo, (nl_item item)) {
  return internal::langinfo(item, internal::thread_locale);
}
} // namespace LIBC_NAMESPACE_DECL
