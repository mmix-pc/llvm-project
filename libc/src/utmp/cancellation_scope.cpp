//===-- Default accounting cancellation policy ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/utmp/cancellation_scope.h"

namespace LIBC_NAMESPACE_DECL {
namespace utmp_backend {

CancellationScope::CancellationScope() = default;
CancellationScope::~CancellationScope() noexcept(false) = default;

} // namespace utmp_backend
} // namespace LIBC_NAMESPACE_DECL
