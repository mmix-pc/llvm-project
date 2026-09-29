//===-- MMIX Linux thread resource preparation ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "resources.h"
#include "hdr/pthread_macros.h"
#include "src/__support/CPP/new.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include <linux/mman.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

static constexpr uintptr_t MAX_ADDRESS = UINTPTR_MAX >> 1;
static constexpr uintptr_t PAGE_SIZE = 8192;
static_assert(alignof(ThreadControl) <= PAGE_SIZE);
static_assert(__is_trivially_destructible(ThreadControl));

static bool round_to_page(size_t value, size_t &rounded) {
  size_t padding = (PAGE_SIZE - value % PAGE_SIZE) % PAGE_SIZE;
  if (value > MAX_ADDRESS || padding > MAX_ADDRESS - value)
    return false;
  rounded = value + padding;
  return true;
}

static ThreadPrepareError map_region(size_t size, int protection,
                                     ThreadMapping &mapping, long &error,
                                     ThreadPrepareError map_error) {
  long ret = __llvm_libc_mmix_syscall(SYS_mmap, 0, size, protection,
                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (static_cast<uintptr_t>(ret) >= static_cast<uintptr_t>(-4095L)) {
    error = -ret;
    return map_error;
  }
  mapping = {static_cast<uintptr_t>(ret), size};
  if (!mapping.base || mapping.base % PAGE_SIZE || mapping.base > MAX_ADDRESS ||
      size > MAX_ADDRESS - mapping.base)
    return ThreadPrepareError::InvalidMapping;
  return ThreadPrepareError::None;
}

static long release_mapping(ThreadMapping &mapping) {
  if (!mapping.size)
    return 0;
  long ret = __llvm_libc_mmix_syscall(SYS_munmap, mapping.base, mapping.size, 0,
                                      0, 0, 0);
  if (ret < 0)
    return -ret;
  mapping = {};
  return 0;
}

ThreadResources thread_resources(const ThreadControl &control) {
  return {control.control_mapping, control.stack_mapping, control.tls};
}

long release_thread_resources(ThreadResources &resources) {
  // Continue independent releases after a failure; descriptors live outside
  // the control and retain every mapping that the kernel did not release.
  TLSResult tls = release_tls(resources.tls);
  long stack = release_mapping(resources.stack);
  long control = release_mapping(resources.control);
  return tls.syscall_error ? tls.syscall_error : stack ? stack : control;
}

static ThreadPreparationResult rollback(ThreadPrepareError error, long syscall,
                                        ThreadResources &resources,
                                        TLSError tls = TLSError::None) {
  long cleanup = release_thread_resources(resources);
  return {error, syscall, tls, cleanup, resources};
}

ThreadPreparationResult prepare_thread(const ThreadPreparation &request,
                                       ThreadControl *&output) {
  const ThreadPreparation input = request;
  if (input.page_size != PAGE_SIZE || input.stack_size < PTHREAD_STACK_MIN ||
      (input.style != ThreadStyle::POSIX && input.style != ThreadStyle::STDC) ||
      (input.style == ThreadStyle::POSIX ? !input.runner.posix_runner
                                         : !input.runner.stdc_runner))
    return {ThreadPrepareError::InvalidAttributes};

  TLSLayout layout;
  TLSError tls_error = tls_layout(input.image, input.page_size, layout);
  if (tls_error != TLSError::None)
    return {ThreadPrepareError::InvalidTLS, 0, tls_error};

  uintptr_t bottom = reinterpret_cast<uintptr_t>(input.stack);
  size_t stack_size = input.stack_size, guard_size = 0, control_size;
  if (!round_to_page(sizeof(ThreadControl), control_size))
    return {ThreadPrepareError::Overflow};
  if (input.stack) {
    if (bottom > MAX_ADDRESS || stack_size > MAX_ADDRESS - bottom)
      return {ThreadPrepareError::Overflow};
    if (bottom % 8 || (bottom + stack_size) % 8)
      return {ThreadPrepareError::InvalidAttributes};
    // A supplied stack's guard is entirely caller-owned. In particular, do
    // not round or validate unused guard arithmetic against the usable range.
  } else if (!round_to_page(stack_size, stack_size) ||
             !round_to_page(input.guard_size, guard_size) ||
             guard_size > MAX_ADDRESS - stack_size) {
    return {ThreadPrepareError::Overflow};
  }

  ThreadResources resources;
  long syscall_error = 0;
  ThreadPrepareError error =
      map_region(control_size, PROT_READ | PROT_WRITE, resources.control,
                 syscall_error, ThreadPrepareError::ControlMapFailed);
  if (error != ThreadPrepareError::None)
    return rollback(error, syscall_error, resources);

  if (!input.stack) {
    error = map_region(stack_size + guard_size,
                       guard_size ? PROT_NONE : PROT_READ | PROT_WRITE,
                       resources.stack, syscall_error,
                       ThreadPrepareError::StackMapFailed);
    if (error != ThreadPrepareError::None)
      return rollback(error, syscall_error, resources);
    bottom = resources.stack.base + guard_size;
    if (guard_size) {
      long ret = __llvm_libc_mmix_syscall(SYS_mprotect, bottom, stack_size,
                                          PROT_READ | PROT_WRITE, 0, 0, 0);
      if (ret < 0)
        return rollback(ThreadPrepareError::ProtectFailed, -ret, resources);
    }
  }

  auto *control =
      new (reinterpret_cast<void *>(resources.control.base)) ThreadControl;
  TLSResult tls =
      allocate_tls(input.image, input.page_size, control, resources.tls);
  if (tls.error != TLSError::None) {
    // allocate_tls may already have tried rollback. Preserve its retained
    // mapping and do not obscure that cleanup failure with an immediate retry.
    auto result = rollback(ThreadPrepareError::TLSFailed, tls.syscall_error,
                           resources, tls.error);
    result.retained.tls = tls.retained_mapping;
    if (tls.retained_mapping.size)
      result.rollback_error = tls.syscall_error;
    return result;
  }

  control->runner = input.runner;
  control->argument = input.argument;
  control->saved_signal_mask = input.saved_signal_mask;
  control->control_mapping = resources.control;
  control->stack_mapping = resources.stack;
  control->tls = resources.tls;
  control->stack_bottom = bottom;
  control->stack_top = bottom + stack_size;
  control->guard_size = guard_size;
  control->owns_stack = !input.stack;
  control->attributes.style = input.style;
  control->attributes.stack = reinterpret_cast<void *>(bottom);
  control->attributes.stacksize = stack_size;
  control->attributes.guardsize = guard_size;
  control->attributes.owned_stack = !input.stack;
  control->attributes.tls = resources.tls.addr;
  control->attributes.tls_size = resources.tls.size;
  control->attributes.detach_state.set(
      uint32_t(input.detached ? DetachState::DETACHED : DetachState::JOINABLE));
  control->attributes.platform_data = &control->clear_tid.value.val;
  control->publish_prepared();
  output = control;
  return {ThreadPrepareError::None};
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
