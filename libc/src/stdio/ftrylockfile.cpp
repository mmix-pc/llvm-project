//===-- Implementation of ftrylockfile
//-------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/ftrylockfile.h"
#include "src/__support/File/file.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, ftrylockfile, (::FILE * stream)) {
  return reinterpret_cast<File *>(stream)->try_lock() ? 0 : 1;
}

} // namespace LIBC_NAMESPACE_DECL
