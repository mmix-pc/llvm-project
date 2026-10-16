//===-- Current-thread DTV refresh -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_DTV_H
#define LLVM_LIBC_LOADER_LINUX_DTV_H

#include "loader/linux/image_registry.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Per-thread state, never shared by live threads. The caller suppresses
// cancellation and pins a published scope's registry snapshot before entry.
// This helper masks signals itself and never allocates a module's TLS image.
// Missing identities retire independent TLS storage on this thread only.
// Failed unmaps retain their entry for retry or terminal reaper cleanup.
class DTVRefresh {
  Address retired_base = 0, retired_size = 0;
  const ThreadTLSAllocation *owner = nullptr;
  long update(const ImageRegistry::Snapshot &, ThreadTLSAllocation &,
              Address page_size, bool unpublished);

public:
  DTVRefresh() = default;
  DTVRefresh(const DTVRefresh &) = delete;
  DTVRefresh &operator=(const DTVRefresh &) = delete;
  // Positive errno, no libc errno access. On a failed old-table unmap the new
  // DTV is already installed; retain this object and retry before retirement.
  // Failed signal-mask restoration is fatal, never an ordinary return.
  long refresh(const ImageRegistry::Snapshot &, ThreadTLSAllocation &,
               Address page_size);
  // Exclusive creator ownership, before clone/publication; never a live peer.
  long prepare(const ImageRegistry::Snapshot &, ThreadTLSAllocation &,
               Address page_size);
  // Exclusive rollback/reclaimer ownership after terminal reference drain.
  long retire();
  bool cleanup_pending() const { return retired_size != 0; }
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
