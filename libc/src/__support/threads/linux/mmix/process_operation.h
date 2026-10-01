//===-- MMIX Linux process-operation admission -------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_PROCESS_OPERATION_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_PROCESS_OPERATION_H

#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
int begin_process_operation();
void end_process_operation();
#else
inline int begin_process_operation() { return 0; }
inline void end_process_operation() {}
#endif

// FIXME: Replace pre-creation-only admission with coordinated fork/credential
// handling. No registry lock is held across callbacks, syscalls or child entry.
class ProcessOperation {
  int error_code = begin_process_operation();

public:
  ProcessOperation() = default;
  ~ProcessOperation() {
    if (!error_code)
      end_process_operation();
  }
  ProcessOperation(const ProcessOperation &) = delete;
  ProcessOperation &operator=(const ProcessOperation &) = delete;
  int error() const { return error_code; }
};
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
