//===-- MMIX Linux unwinder configuration ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBUNWIND_MMIX_LINUX_CONFIG_H
#define LIBUNWIND_MMIX_LINUX_CONFIG_H

#if !defined(_LIBUNWIND_IS_NATIVE_ONLY) || !defined(_LIBUNWIND_HAS_NO_THREADS)
#error "MMIX Linux libunwind requires native single-thread configuration"
#endif
#if defined(_LIBUNWIND_IS_BAREMETAL) || defined(_LIBUNWIND_SUPPORT_FRAME_APIS)
#error "MMIX Linux libunwind requires static Linux frame discovery"
#endif

// FIXME: Add loader-aware frame discovery and symbol lookup with shared loading.
#define _LIBUNWIND_MMIX_LINUX 1
#define _LIBUNWIND_SUPPORT_DWARF_UNWIND 1
#define _LIBUNWIND_SUPPORT_DWARF_INDEX 1
#define _LIBUNWIND_USE_DLADDR 0

#endif
