//===-- Startup thread TLS allocation -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/tls_allocation.h"
#include "hdr/errno_macros.h"
#include "hdr/sys_mman_macros.h"
#include "loader/linux/syscall.h"
#include "loader/linux/target.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace loader {

static bool round_mapping(Address bytes, Address page, Address &out) {
  if (!bytes || bytes > Address(__LONG_MAX__) - (page - 1))
    return false;
  out = (bytes + page - 1) & ~(page - 1);
  return true;
}

long release_thread_tls(void *, ThreadTLSAllocation *allocation) {
  if (!allocation)
    return EINVAL;
  if (allocation->runtime_owner)
    return EINVAL; // Runtime ownership must be drained by its lifecycle service.
  // Invalidate access even when a failed unmap retains ownership for retry.
  allocation->tp = allocation->registry_cookie = 0;
  long error = 0;
  auto release = [&](uint64_t &base, uint64_t &size) {
    if (!size)
      return;
    long result = raw_syscall(SYS_munmap, static_cast<long>(base),
                              static_cast<long>(size));
    if (result < 0) {
      if (!error)
        error = -result;
    } else {
      base = size = 0;
    }
  };
  release(allocation->dtv_mapping_base, allocation->dtv_mapping_size);
  release(allocation->tls_mapping_base, allocation->tls_mapping_size);
  return error;
}

long prepare_thread_tls(void *context, void *thread_state,
                        ThreadTLSAllocation *allocation) {
  if (!context || !allocation)
    return EINVAL;
  if (allocation->tp || allocation->tls_mapping_base ||
      allocation->tls_mapping_size || allocation->dtv_mapping_base ||
      allocation->dtv_mapping_size || allocation->registry_cookie)
    return EBUSY;
  if (allocation->runtime_owner)
    return EBUSY;
  const auto &ctx = *static_cast<const TLSAllocationContext *>(context);
  const auto *set = ctx.relocated ? ctx.relocated->modules() : nullptr;
  if (!set || !is_supported_page_size(ctx.page_size))
    return EINVAL;
  Address tls_size, dtv_size;
  Address alignment = set->tp_alignment;
  if (!alignment || (alignment & (alignment - 1)) ||
      set->tls_extent > ~Address(0) - (alignment - 1) ||
      !round_mapping(set->tls_extent + alignment - 1, ctx.page_size,
                     tls_size) ||
      set->module_count >=
          (~Address(0) - sizeof(StartupDTV)) / sizeof(StartupDTVEntry) ||
      !round_mapping(sizeof(StartupDTV) +
                         (set->module_count + 1) * sizeof(StartupDTVEntry),
                     ctx.page_size, dtv_size))
    return EOVERFLOW;
  long result =
      raw_syscall(SYS_mmap, 0, static_cast<long>(tls_size),
                  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (result < 0)
    return -result;
  allocation->tls_mapping_base = result;
  allocation->tls_mapping_size = tls_size;
  Address base = static_cast<Address>(result);
  if (base > ~Address(0) - tls_size) {
    (void)release_thread_tls(context, allocation);
    return EOVERFLOW;
  }
  Address tp = (base + alignment - 1) & ~(alignment - 1);
  result =
      raw_syscall(SYS_mmap, 0, static_cast<long>(dtv_size),
                  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (result < 0) {
    long error = -result;
    (void)release_thread_tls(context, allocation);
    return error;
  }
  allocation->dtv_mapping_base = result;
  allocation->dtv_mapping_size = dtv_size;
  if (static_cast<Address>(result) > ~Address(0) - dtv_size) {
    (void)release_thread_tls(context, allocation);
    return EOVERFLOW;
  }
  // Volatile byte loops keep bootstrap copies independent of libc helpers.
  auto *bytes = reinterpret_cast<volatile unsigned char *>(tp);
  for (Address i = 0; i < set->tls_extent; ++i)
    bytes[i] = 0;
  auto *dtv = reinterpret_cast<StartupDTV *>(result);
  auto *entries = reinterpret_cast<StartupDTVEntry *>(result + sizeof(*dtv));
  dtv->capacity = set->module_count + 1;
  dtv->generation = set->generation;
  dtv->entries = reinterpret_cast<uint64_t>(entries);
  dtv->runtime_owner = 0;
  for (Address i = 0; i <= set->module_count; ++i) {
    entries[i].generation = entries[i].image_base = 0;
    entries[i].allocation_owner = entries[i].allocation_extent = 0;
  }
  for (Address i = 0; i < set->module_count; ++i) {
    const auto &module = set->modules[i];
    auto *source = reinterpret_cast<const volatile unsigned char *>(
        module.template_address);
    for (Address j = 0; j < module.file_size; ++j)
      bytes[module.tp_offset + j] = source[j];
    auto &entry = entries[module.module_id];
    entry.generation = module.generation;
    entry.image_base = tp + module.tp_offset;
  }
  auto *header = reinterpret_cast<uint64_t *>(tp);
  header[0] = reinterpret_cast<uint64_t>(dtv);
  header[1] = reinterpret_cast<uint64_t>(thread_state);
  allocation->registry_cookie = reinterpret_cast<uint64_t>(set);
  allocation->tp = tp;
  return 0;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
