//===-- Implementation of getopt_long -------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/getopt/getopt_long.h"
#include "src/__support/common.h"
#include "src/stdlib/getenv.h"
#include "src/unistd/getopt.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, getopt_long,
                   (int argc, char *const argv[], const char *optstring,
                    const struct option *options, int *index)) {
  return impl::getopt_internal(argc, argv, optstring, options, index,
                               LIBC_NAMESPACE::getenv("POSIXLY_CORRECT") ==
                                   nullptr);
}
} // namespace LIBC_NAMESPACE_DECL
