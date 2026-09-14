//===-- MMIX Linux process arguments ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_STARTUP_LINUX_MMIX_PROCESS_ARGS_H
#define LLVM_LIBC_STARTUP_LINUX_MMIX_PROCESS_ARGS_H

#include "config/linux/app.h"
#include "src/__support/OSUtil/linux/auxv.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

struct ProcessArgs {
  Args *args;
  uintptr_t *env;
  const auxv::Entry *aux;
  const unsigned char *random;
  uintptr_t page_size;
};

// Accessible, terminated initial memory is a kernel obligation. This checks
// representable addresses and the supported metadata, not memory mappings.
bool parse_process_args(uintptr_t *stack, ProcessArgs &result);
void publish_process_args(const ProcessArgs &args);

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
