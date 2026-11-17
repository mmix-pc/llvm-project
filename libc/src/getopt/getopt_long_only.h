//===-- Implementation header for getopt_long_only --------------------*- C++
//-*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_SRC_GETOPT_GETOPT_LONG_ONLY_H
#define LLVM_LIBC_SRC_GETOPT_GETOPT_LONG_ONLY_H
#include "hdr/types/struct_option.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
int getopt_long_only(int argc, char *const argv[], const char *optstring,
                     const struct option *options, int *index);
}
#endif
