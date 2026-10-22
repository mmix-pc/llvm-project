//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implementation of mkstemp, a POSIX function that creates a unique temporary
/// file from a template string ending in at least six 'X' characters.
///
/// Replaces the trailing X's with random characters from the POSIX portable
/// filename character set, opens the file exclusively, and returns an open
/// file descriptor, retrying automatically on name collision. See:
/// https://pubs.opengroup.org/onlinepubs/9799919799/functions/mkdtemp.html
///
//===----------------------------------------------------------------------===//

#include "src/stdlib/mkstemp.h"
#include "src/stdlib/mkstemp_impl.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mkstemp, (char *tmpl)) {
  return internal::create_temporary_file(tmpl, 0);
}

} // namespace LIBC_NAMESPACE_DECL
