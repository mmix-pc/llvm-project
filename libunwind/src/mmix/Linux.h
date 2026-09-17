//===-- MMIX Linux frame provider interface -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBUNWIND_MMIX_LINUX_H
#define LIBUNWIND_MMIX_LINUX_H

namespace libunwind {
struct UnwindInfoSections;

// A required provider, not a successful fallback for missing Linux frames.
_LIBUNWIND_HIDDEN bool findMMIXLinuxUnwindSections(uintptr_t targetAddr,
                                                UnwindInfoSections &info);
} // namespace libunwind

#endif
