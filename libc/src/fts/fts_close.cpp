//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "fts_close.h"
#include "engine.h"
#include "src/__support/common.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, fts_close, (FTS * stream)) {
  return fts::walk_close(stream);
}
} // namespace LIBC_NAMESPACE_DECL
