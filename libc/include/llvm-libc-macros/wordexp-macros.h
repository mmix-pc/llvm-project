//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_WORDEXP_MACROS_H
#define LLVM_LIBC_MACROS_WORDEXP_MACROS_H

#define WRDE_DOOFFS (1 << 0)
#define WRDE_APPEND (1 << 1)
#define WRDE_NOCMD (1 << 2)
#define WRDE_REUSE (1 << 3)
#define WRDE_SHOWERR (1 << 4)
#define WRDE_UNDEF (1 << 5)

#define WRDE_NOSPACE 1
#define WRDE_BADCHAR 2
#define WRDE_BADVAL 3
#define WRDE_CMDSUB 4
#define WRDE_SYNTAX 5

#endif
