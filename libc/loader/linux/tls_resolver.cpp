//===-- Startup TLS resolver ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/tls_resolver.h"
#include "loader/linux/io.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {
void *resolve_startup_tls(void *context, const TLSIndex *index) {
  if (!context || !index ||
      reinterpret_cast<Address>(index) % alignof(TLSIndex))
    fatal_startup();
  const auto &ctx = *static_cast<const TLSAllocationContext *>(context);
  const auto *set = ctx.relocated ? ctx.relocated->modules() : nullptr;
  Address tp = current_thread_pointer();
  if (!set || !set->modules || set->generation != 1 || !set->tp_alignment ||
      tp == 0 || tp % set->tp_alignment || set->tls_extent > ~Address(0) - tp ||
      index->module_id == 0 || index->module_id > set->module_count ||
      set->module_count == ~Address(0))
    fatal_startup();
  const auto &module = set->modules[index->module_id - 1];
  if (module.module_id != index->module_id || module.generation != 1 ||
      module.tp_offset > set->tls_extent ||
      module.memory_size > set->tls_extent - module.tp_offset ||
      index->offset > module.memory_size)
    fatal_startup();
  Address pointer = *reinterpret_cast<const Address *>(tp);
  if (!pointer || pointer % alignof(StartupDTV))
    fatal_startup();
  const auto &dtv = *reinterpret_cast<const StartupDTV *>(pointer);
  if (dtv.capacity != set->module_count + 1 ||
      dtv.generation != set->generation || !dtv.entries ||
      dtv.entries % alignof(StartupDTVEntry) ||
      dtv.capacity > (~Address(0) - dtv.entries) / sizeof(StartupDTVEntry))
    fatal_startup();
  const auto &entry =
      reinterpret_cast<const StartupDTVEntry *>(dtv.entries)[index->module_id];
  if (entry.generation != module.generation ||
      entry.image_base != tp + module.tp_offset || entry.allocation_owner ||
      entry.allocation_extent)
    fatal_startup();
  return reinterpret_cast<void *>(entry.image_base + index->offset);
}
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
