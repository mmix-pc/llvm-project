//===-- Current-thread DTV refresh ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/dtv.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/sys_mman_macros.h"
#include "loader/linux/io.h"
#include "loader/linux/syscall.h"
#include "loader/linux/target.h"
#include "loader/linux/tls_resolver.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace loader {

static bool contains(Address base, Address size, Address address,
                     Address bytes) {
  return base && size <= ~Address(0) - base && address >= base &&
         address - base <= size && bytes <= size - (address - base);
}

static bool empty(const StartupDTVEntry &entry) {
  return !entry.generation && !entry.image_base && !entry.allocation_owner &&
         !entry.allocation_extent;
}

static long validate(const ImageRegistry::Snapshot &snapshot,
                     const ThreadTLSAllocation &allocation,
                     const StartupDTV &dtv, Address &required) {
  if (!snapshot.generation || snapshot.images.empty() || !dtv.generation ||
      dtv.generation > snapshot.generation || !dtv.capacity ||
      dtv.entries % alignof(StartupDTVEntry) ||
      dtv.entries < allocation.dtv_mapping_base + sizeof(StartupDTV) ||
      dtv.capacity > ~Address(0) / sizeof(StartupDTVEntry) ||
      !contains(allocation.dtv_mapping_base, allocation.dtv_mapping_size,
                dtv.entries, dtv.capacity * sizeof(StartupDTVEntry)))
    return EINVAL;
  required = 1;
  for (const auto *image : snapshot.images) {
    if (!image || !image->tls.module_id || !image->tls.generation ||
        image->tls.generation > snapshot.generation)
      return EINVAL;
    if (image->tls.module_id == ~Address(0))
      return EOVERFLOW;
    if (image->tls.module_id + 1 > required)
      required = image->tls.module_id + 1;
    // Startup modules must already have their original TP-relative entry.
    if (image->startup && image->tls.module_id >= dtv.capacity)
      return EINVAL;
  }
  const auto *entries = reinterpret_cast<const StartupDTVEntry *>(dtv.entries);
  for (Address i = 0; i < dtv.capacity; ++i) {
    const auto &entry = entries[i];
    const ImageRegistry::Image *image = nullptr;
    for (const auto *candidate : snapshot.images)
      if (candidate->tls.module_id == i) {
        image = candidate;
        break;
      }
    if (empty(entry)) {
      if (image && image->startup)
        return EINVAL;
      continue;
    }
    if (!i || !entry.generation || entry.generation > dtv.generation)
      return EINVAL;
    if (!image) {
      // Removed identities are never reused. Only independently owned late
      // storage may survive removal; startup entries must remain discoverable.
      if (!contains(entry.allocation_owner, entry.allocation_extent,
                    entry.image_base, 1) ||
          entry.allocation_owner == allocation.tls_mapping_base ||
          entry.allocation_owner == allocation.dtv_mapping_base)
        return EINVAL;
      continue;
    }
    if (entry.generation != image->tls.generation)
      return EINVAL;
    const auto &module = image->tls;
    if (!module.alignment || (module.alignment & (module.alignment - 1)) ||
        module.residue >= module.alignment ||
        (entry.image_base & (module.alignment - 1)) != module.residue)
      return EINVAL;
    if (image->startup) {
      if (module.tp_offset > ~Address(0) - allocation.tp ||
          entry.image_base != allocation.tp + module.tp_offset ||
          entry.allocation_owner || entry.allocation_extent ||
          !contains(allocation.tls_mapping_base, allocation.tls_mapping_size,
                    entry.image_base, module.memory_size))
        return EINVAL;
    } else if (!contains(entry.allocation_owner, entry.allocation_extent,
                         entry.image_base, module.memory_size))
      return EINVAL;
  }
  return 0;
}

long DTVRefresh::refresh(const ImageRegistry::Snapshot &snapshot,
                         ThreadTLSAllocation &allocation, Address page) {
  return update(snapshot, allocation, page, false);
}

long DTVRefresh::prepare(const ImageRegistry::Snapshot &snapshot,
                        ThreadTLSAllocation &allocation, Address page) {
  return update(snapshot, allocation, page, true);
}

long DTVRefresh::retire() {
  if (!retired_size)
    return 0;
  long result = raw_syscall(SYS_munmap, retired_base, retired_size);
  if (result)
    return result < 0 ? -result : EIO;
  retired_base = retired_size = 0;
  owner = nullptr;
  return 0;
}

long DTVRefresh::update(const ImageRegistry::Snapshot &snapshot,
                       ThreadTLSAllocation &allocation, Address page,
                       bool unpublished) {
  if (!is_supported_page_size(page) || !allocation.tp ||
      (!unpublished && allocation.tp != current_thread_pointer()) ||
      allocation.tp % alignof(Address) ||
      !contains(allocation.tls_mapping_base, allocation.tls_mapping_size,
                allocation.tp, startup_tls_header_size()) ||
      (owner && owner != &allocation))
    return EINVAL;

  // Linux kernel sigsets contain one 64-bit word here, independent of libc's
  // public sigset_t. Internal cancellation notification is masked as well.
  uint64_t full = ~uint64_t(0), saved = 0;
  long result =
      raw_syscall(SYS_rt_sigprocmask, SIG_BLOCK, reinterpret_cast<long>(&full),
                  reinterpret_cast<long>(&saved), sizeof(full));
  if (result)
    return result < 0 ? -result : EIO;
  auto restore = [&](long error) {
    if (raw_syscall(SYS_rt_sigprocmask, SIG_SETMASK,
                    reinterpret_cast<long>(&saved), 0, sizeof(saved)))
      fatal_startup();
    return error;
  };
  if (retired_size) {
    result = raw_syscall(SYS_munmap, retired_base, retired_size);
    if (result)
      return restore(result < 0 ? -result : EIO);
    retired_base = retired_size = 0;
    owner = nullptr;
  }
  auto *slot = reinterpret_cast<Address *>(allocation.tp);
  Address pointer = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
  if (pointer != allocation.dtv_mapping_base || pointer % alignof(StartupDTV) ||
      !contains(allocation.dtv_mapping_base, allocation.dtv_mapping_size,
                pointer, sizeof(StartupDTV)))
    return restore(EINVAL);
  auto *old = reinterpret_cast<StartupDTV *>(pointer);
  Address required;
  long error = validate(snapshot, allocation, *old, required);
  if (error)
    return restore(error);
  // Only the owner (or unpublished creator) edits this DTV. No image/template
  // access is needed to reclaim a removed module. Keep failed mappings in the
  // entry for retry or terminal cleanup, and never reuse their module IDs.
  auto *old_entries = reinterpret_cast<StartupDTVEntry *>(old->entries);
  for (Address i = 1; i < old->capacity; ++i) {
    auto &entry = old_entries[i];
    bool present = false;
    for (const auto *image : snapshot.images)
      present |= image->tls.module_id == i;
    if (empty(entry) || present)
      continue;
    result = raw_syscall(SYS_munmap, entry.allocation_owner,
                        entry.allocation_extent);
    if (result)
      return restore(result < 0 ? -result : EIO);
    entry = {};
  }
  if (required <= old->capacity) {
    __atomic_store_n(&old->generation, snapshot.generation, __ATOMIC_RELEASE);
    return restore(0);
  }
  constexpr Address limit = __LONG_MAX__;
  const Address max_capacity =
      (limit - (page - 1) - sizeof(StartupDTV)) / sizeof(StartupDTVEntry);
  if (required > max_capacity)
    return restore(EOVERFLOW);
  Address capacity = required;
  if (old->capacity <= max_capacity / 2 && capacity < old->capacity * 2)
    capacity = old->capacity * 2;
  Address size =
      (sizeof(StartupDTV) + capacity * sizeof(StartupDTVEntry) + page - 1) &
      ~(page - 1);
  result = raw_syscall(SYS_mmap, 0, size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (result < 0)
    return restore(-result);
  // Raw Linux mmap supplies aligned, zero-filled owned storage. No callback
  // or other thread may mutate the source DTV during this copy.
  auto *next = reinterpret_cast<StartupDTV *>(result);
  auto *entries = reinterpret_cast<StartupDTVEntry *>(result + sizeof(*next));
  next->capacity = capacity;
  next->generation = snapshot.generation;
  next->entries = reinterpret_cast<Address>(entries);
  next->runtime_owner = old->runtime_owner;
  const auto *source = reinterpret_cast<const StartupDTVEntry *>(old->entries);
  for (Address i = 0; i < old->capacity; ++i) {
    entries[i].generation = source[i].generation;
    entries[i].image_base = source[i].image_base;
    entries[i].allocation_owner = source[i].allocation_owner;
    entries[i].allocation_extent = source[i].allocation_extent;
  }
  retired_base = allocation.dtv_mapping_base;
  retired_size = allocation.dtv_mapping_size;
  owner = &allocation;
  allocation.dtv_mapping_base = result;
  allocation.dtv_mapping_size = size;
  __atomic_store_n(slot, Address(result), __ATOMIC_RELEASE);
  result = raw_syscall(SYS_munmap, retired_base, retired_size);
  if (!result) {
    retired_base = retired_size = 0;
    owner = nullptr;
  }
  return restore(result < 0 ? -result : result ? EIO : 0);
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
