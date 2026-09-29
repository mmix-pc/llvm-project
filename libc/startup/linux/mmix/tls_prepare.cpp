//===-- MMIX Linux pre-TP preparation -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/threads/linux/mmix/tls.h"
#include "tls_startup.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
LIBC_CONSTINIT StartupState state;
bool attempted = false;
} // namespace

StartupState *prepare_tls_startup(uintptr_t *stack) {
  if (attempted)
    return nullptr;
  attempted = true;
  if (!parse_process_args(stack, state.args))
    return nullptr;
  long tid = syscall_impl(SYS_gettid);
  if (tid <= 0 || tid > cpp::numeric_limits<int>::max())
    return nullptr;
  state.thread.attributes.tid = static_cast<int>(tid);
  TLSResult result = allocate_tls(state.args.tls, state.args.page_size,
                                  &state.thread, state.tls);
  // Allocation attempts rollback on failure; the caller terminates.
  if (result.error != TLSError::None)
    return nullptr;
  auto &attributes = state.thread.attributes;
  attributes.tls = state.tls.addr;
  attributes.tls_size = state.tls.size;
  attributes.platform_data = &state.thread.clear_tid.value;
  return &state;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
