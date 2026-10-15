//===-- MMIX interpreter debugger rendezvous
//-------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/runtime_state.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {
extern "C" {
__attribute__((visibility("protected"))) r_debug _r_debug{};
}
r_debug *runtime_debug_record() { return &_r_debug; }
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
