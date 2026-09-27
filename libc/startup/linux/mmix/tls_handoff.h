//===-- MMIX Linux nonreturning TLS handoff ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_STARTUP_LINUX_MMIX_TLS_HANDOFF_H
#define LLVM_LIBC_STARTUP_LINUX_MMIX_TLS_HANDOFF_H

#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
struct StartupState;
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

// The caller has initialized the TLS image and global guard. This is an opaque
// assembly boundary, never a returning TP setter or a memory-only barrier.
extern "C" [[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_linux_tls_handoff(__UINTPTR_TYPE__ tp,
                                  LIBC_NAMESPACE::mmix::StartupState *state);
extern "C" [[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_linux_post_tls(LIBC_NAMESPACE::mmix::StartupState *state);

#endif
