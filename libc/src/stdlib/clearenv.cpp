//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/clearenv.h"
#include "src/__support/common.h"
#include "src/stdlib/environ_internal.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, clearenv, ()) {
  internal::EnvironmentManager::get_instance().clear();
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
