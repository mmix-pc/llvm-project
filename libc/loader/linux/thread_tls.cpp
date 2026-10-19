//===-- Runtime thread TLS lifecycle --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/thread_tls.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/sys_mman_macros.h"
#include "loader/linux/io.h"
#include "loader/linux/runtime_tls.h"
#include "loader/linux/syscall.h"
#include "loader/linux/target.h"
#include "loader/linux/tls_resolver.h"
#include "src/__support/CPP/new.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace loader {
namespace {
struct State {
  ThreadTLSAllocation allocation{};
  DTVRefresh refresh;
  const ThreadTLSContext *context;
  Address extent;
  bool retiring = false;
  bool prepared = false;
  State(const ThreadTLSContext *context, Address extent)
      : context(context), extent(extent) {}
};

bool empty(const ThreadTLSAllocation &a) {
  return !a.tp && !a.tls_mapping_base && !a.tls_mapping_size &&
         !a.dtv_mapping_base && !a.dtv_mapping_size && !a.registry_cookie &&
         !a.runtime_owner;
}

void copy_owner(ThreadTLSAllocation &out, State &state) {
  out = state.allocation;
  out.runtime_owner = reinterpret_cast<Address>(&state);
}
} // namespace

long release_runtime_thread_tls(void *context, ThreadTLSAllocation *out) {
  if (!context || !out)
    return EINVAL;
  if (!out->runtime_owner)
    return empty(*out) ? 0 : EINVAL;
  auto &state = *reinterpret_cast<State *>(out->runtime_owner);
  auto &a = state.allocation;
  if (state.context != context)
    return EINVAL;
  if (a.tp && a.tp == current_thread_pointer())
    return EBUSY;
  state.retiring = true;
  // Retirement never needs the obsolete owner pointer to a pthread control.
  // Keep the stable state and table until every independent mapping is gone.
  long error = state.refresh.retire();
  if (state.prepared && a.dtv_mapping_size) {
    auto &dtv = *reinterpret_cast<StartupDTV *>(a.dtv_mapping_base);
    auto *entries = reinterpret_cast<StartupDTVEntry *>(dtv.entries);
    for (Address i = 0; i < dtv.capacity; ++i) {
      auto &entry = entries[i];
      if (!entry.allocation_extent)
        continue; // Startup entries borrow the common block.
      long result = raw_syscall(SYS_munmap, entry.allocation_owner,
                                entry.allocation_extent);
      if (result) {
        if (!error)
          error = result < 0 ? -result : EIO;
      } else {
        entry = {};
      }
    }
  }
  if (!error)
    error = release_thread_tls(nullptr, &a);
  copy_owner(*out, state);
  out->tp = out->registry_cookie = 0;
  if (error)
    return error;
  Address extent = state.extent;
  long result = raw_syscall(SYS_munmap, out->runtime_owner, extent);
  if (result)
    return result < 0 ? -result : EIO;
  *out = {};
  return 0;
}

long prepare_runtime_thread_tls(void *context, void *thread,
                                ThreadTLSAllocation *out) {
  if (!context || !out)
    return EINVAL;
  if (!empty(*out))
    return EBUSY;
  auto &ctx = *static_cast<ThreadTLSContext *>(context);
  Address page = ctx.startup.page_size;
  if (!is_supported_page_size(page) || sizeof(State) > page)
    return EINVAL;
  // Acquire before any pthread insertion. A later publication is observed by
  // the worker's first resolver call; no writer/registry lock spans cloning.
  if (!ctx.scopes)
    return EINVAL;
  auto lease = ctx.scopes->acquire();
  const auto *scope = lease.get();
  if (!scope)
    return EINVAL;
  const auto *view = scope->registry;
  long mapped = raw_syscall(SYS_mmap, 0, page, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mapped < 0)
    return -mapped;
  auto &state = *new (reinterpret_cast<void *>(mapped)) State(&ctx, page);
  long error = prepare_thread_tls(&ctx.startup, thread, &state.allocation);
  if (!error) {
    state.prepared = true;
    auto *dtv =
        reinterpret_cast<StartupDTV *>(state.allocation.dtv_mapping_base);
    dtv->runtime_owner = mapped;
    error = state.refresh.prepare(*view, state.allocation, page);
  }
  copy_owner(*out, state);
  if (error)
    (void)release_runtime_thread_tls(context, out);
  return error;
}

void *peek_runtime_thread_tls(void *context, const StartupTLSModule *module) {
  Address tp = current_thread_pointer();
  if (!context || !module || !module->module_id || !tp)
    return nullptr;
  Address pointer =
      __atomic_load_n(reinterpret_cast<Address *>(tp), __ATOMIC_ACQUIRE);
  const auto *dtv = reinterpret_cast<const StartupDTV *>(pointer);
  if (!dtv || !dtv->runtime_owner || module->module_id >= dtv->capacity)
    return nullptr;
  const auto &state = *reinterpret_cast<const State *>(dtv->runtime_owner);
  if (state.context != context || state.retiring || state.allocation.tp != tp)
    return nullptr;
  const auto &ctx = *static_cast<const ThreadTLSContext *>(context);
  if (!ctx.scopes)
    return nullptr;
  auto lease = ctx.scopes->acquire();
  if (!lease.get() ||
      !lease.get()->registry->find(module->module_id, module->generation))
    return nullptr;
  const auto &entry = reinterpret_cast<const StartupDTVEntry *>(
      dtv->entries)[module->module_id];
  if (__atomic_load_n(&entry.generation, __ATOMIC_ACQUIRE) !=
      module->generation)
    return nullptr;
  return reinterpret_cast<void *>(entry.image_base);
}

void *resolve_runtime_thread_tls(void *context, const TLSIndex *index) {
  Address tp = current_thread_pointer();
  if (!context || !tp)
    return resolve_runtime_tls(nullptr, index); // Contracted fatal diagnostic.
  Address pointer =
      __atomic_load_n(reinterpret_cast<Address *>(tp), __ATOMIC_ACQUIRE);
  const auto *dtv = reinterpret_cast<const StartupDTV *>(pointer);
  if (!dtv || !dtv->runtime_owner)
    return resolve_runtime_tls(nullptr, index);
  auto &state = *reinterpret_cast<State *>(dtv->runtime_owner);
  auto &ctx = *static_cast<ThreadTLSContext *>(context);
  if (state.context != &ctx || state.retiring || state.allocation.tp != tp)
    return resolve_runtime_tls(nullptr, index);
  if (!ctx.scopes)
    return resolve_runtime_tls(nullptr, index);
  auto lease = ctx.scopes->acquire();
  const auto *scope = lease.get();
  RuntimeTLSContext lookup{scope ? scope->registry : nullptr, &state.allocation,
                           &state.refresh, ctx.startup.page_size};
  return resolve_runtime_tls(&lookup, index);
}

void *try_resolve_runtime_thread_tls(void *context, const TLSIndex *index) {
  if (!context || !index ||
      reinterpret_cast<Address>(index) % alignof(TLSIndex) || !index->module_id)
    return nullptr;
  // Protect the reader lease against nonlocal handler exits and cancellation.
  // DTV mutation also masks signals and is confined to this thread; another
  // thread may retire metadata, but cannot reclaim this thread's TLS storage.
  uint64_t full = ~uint64_t(0), saved = 0;
  if (raw_syscall(SYS_rt_sigprocmask, SIG_BLOCK, reinterpret_cast<long>(&full),
                  reinterpret_cast<long>(&saved), sizeof(full)))
    return nullptr;
  void *result = [&]() -> void * {
    Address tp = current_thread_pointer();
    if (!tp)
      return nullptr;
    Address pointer =
        __atomic_load_n(reinterpret_cast<Address *>(tp), __ATOMIC_ACQUIRE);
    const auto *dtv = reinterpret_cast<const StartupDTV *>(pointer);
    if (!dtv || !dtv->runtime_owner || index->module_id >= dtv->capacity)
      return nullptr;
    const auto &state = *reinterpret_cast<const State *>(dtv->runtime_owner);
    const auto &ctx = *static_cast<const ThreadTLSContext *>(context);
    if (state.context != &ctx || state.retiring || state.allocation.tp != tp ||
        !ctx.scopes)
      return nullptr;
    auto lease = ctx.scopes->acquire();
    if (!lease.get())
      return nullptr;
    const auto &entry = reinterpret_cast<const StartupDTVEntry *>(
        dtv->entries)[index->module_id];
    Address generation =
        __atomic_load_n(&entry.generation, __ATOMIC_ACQUIRE);
    const auto *image =
        lease.get()->registry->find(index->module_id, generation);
    if (!generation || !image || index->offset > image->tls.memory_size ||
        !entry.image_base || index->offset > ~Address(0) - entry.image_base)
      return nullptr;
    return reinterpret_cast<void *>(entry.image_base + index->offset);
  }(); // Drop the nonallocating snapshot lease before restoring signals.
  if (raw_syscall(SYS_rt_sigprocmask, SIG_SETMASK,
                  reinterpret_cast<long>(&saved), 0, sizeof(saved)))
    fatal_startup();
  return result;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
