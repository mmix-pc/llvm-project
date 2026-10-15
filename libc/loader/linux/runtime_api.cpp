//===-- Resident runtime loading services ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/runtime_api.h"
#include "loader/linux/io.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {
long RuntimeAPI::run_startup(RuntimeInitializers::Batch &batch,
                             LoaderCancellation hooks) {
  LoaderLocks::Session session(locks, hooks);
  LoaderLocks::Guard guard(session, LoaderLocks::Kind::Mutation);
  if (guard.error())
    return guard.error();
  return initializers.run(batch, session, stack, invoke);
}

long RuntimeAPI::finalize(LoaderCancellation hooks) {
  LoaderLocks::Session session(locks, hooks);
  LoaderLocks::Guard guard(session, LoaderLocks::Kind::Mutation);
  return guard.error() ? guard.error() : initializers.finalize(session);
}

static long string_view(const char *text, Bytes &out,
                        size_t limit = __SIZE_MAX__) {
  if (!text)
    return EINVAL;
  size_t n = 0;
  while (n < limit && text[n])
    ++n;
  if (n == limit)
    return ENAMETOOLONG;
  if (!n)
    return EINVAL;
  out = {reinterpret_cast<const unsigned char *>(text), n};
  return 0;
}

long RuntimeAPI::Request::reset() {
  long error = initialization.abort();
  if (!error)
    error = scope.abort();
  if (!error)
    error = load.abort();
  return error;
}

long RuntimeAPI::open(const char *name, int flags, uintptr_t pc,
                      LoaderCancellation cancellation, uint64_t *out) {
  if (!out)
    return EINVAL;
  *out = 0;
  if (!valid_open_flags(static_cast<unsigned>(flags)))
    return EINVAL;
  Bytes path;
  if (name)
    if (long error = string_view(name, path, DependencySearch::PATH_CAPACITY))
      return error;
  LoaderLocks::Session session(locks, cancellation);
  LoaderLocks::Guard mutation(session, LoaderLocks::Kind::Mutation);
  if (mutation.error())
    return -mutation.error();
  auto *snapshot = scopes.snapshot();
  const ImageRegistry::Image *caller;
  if (!snapshot || snapshot->registry != images.snapshot() ||
      RuntimeScopes::caller(*snapshot, pc, caller) != SymbolError::None)
    return EINVAL;
  if (long error = refresh_queries())
    return error;
  if (pending) {
    long error = pending->reset();
    if (error)
      return -error;
  } else {
    void *storage;
    long error = resident.allocate(sizeof(Request), alignof(Request), storage);
    if (error)
      return -error;
    pending = new (storage) Request(page);
  }
  auto *request = pending;
  long error;
  uint64_t token = 0;
  if (!name) {
    error = scopes.prepare_open(*snapshot->registry, nullptr, false,
                                request->scope, token);
    if (!error)
      error = scopes.prepare_unwind(request->scope);
    if (!error)
      error = scopes.publish(request->scope);
  } else {
    auto result =
        request->load.prepare(images, scopes, *caller, path, environment,
                              flags & OpenGlobal, flags & OpenNoLoad);
    error = result.cleanup_error ? result.cleanup_error : result.error;
    if (!error)
      error = initializers.prepare(*request->load.view()->registry,
                                   request->initialization);
    if (!error)
      error = queries.prepare(*request->load.view()->registry);
    if (!error)
      error = request->load.commit(token);
    if (!error &&
        initializers.attach(request->initialization, *images.snapshot()))
      fatal_startup(); // No fallible work or callback may split commit/attach.
  }
  if (error) {
    long query_cleanup = queries.abort();
    long cleanup = request->reset();
    return cleanup ? -cleanup : query_cleanup ? -query_cleanup : -error;
  }
  queries.publish();
  // Published request storage remains resident. Release the working slot
  // before callbacks so a nested open owns an independent transaction.
  pending = nullptr;
  if (name) {
    error = initializers.run(request->initialization, session, stack, invoke);
    if (error)
      return -error; // Process termination does not roll back user effects.
  }
  *out = token;
  return 0;
}

long RuntimeAPI::lookup(uint64_t handle, const char *name, uintptr_t pc,
                        LoaderCancellation cancellation, uintptr_t *out) {
  if (!out)
    return EINVAL;
  *out = 0;
  Bytes symbol;
  if (long error = string_view(name, symbol))
    return error;
  LoaderLocks::Session session(locks, cancellation);
  LoaderLocks::Guard mutation(session, LoaderLocks::Kind::Mutation);
  if (mutation.error())
    return -mutation.error();
  const auto *snapshot = scopes.snapshot();
  const ImageRegistry::Image *caller;
  if (!snapshot ||
      RuntimeScopes::caller(*snapshot, pc, caller) != SymbolError::None)
    return EINVAL;
  RuntimeScopes::Value result;
  SymbolError error;
  if (handle == LookupDefault)
    error = RuntimeScopes::lookup_default(*snapshot, caller, symbol, result);
  else if (handle == LookupNext)
    error = RuntimeScopes::lookup_next(*snapshot, caller, symbol, result);
  else {
    bool valid = false;
    for (const auto &candidate : snapshot->handles)
      valid |= candidate.token == handle;
    if (!valid)
      return EINVAL;
    error = RuntimeScopes::lookup(*snapshot, handle, symbol, result);
  }
  if (error != SymbolError::None)
    return ENOENT;
  if (result.symbol.tls) {
    if (!result.image || !tls.context || !tls.resolve)
      return ENOSYS;
    TLSIndex index{result.image->tls.module_id, result.symbol.address};
    void *address = tls.resolve(tls.context, &index);
    if (!address)
      fatal_startup(); // Same allocation/failure policy as GD/LD resolution.
    *out = reinterpret_cast<uintptr_t>(address);
  } else
    *out = result.symbol.address;
  return 0;
}

long RuntimeAPI::close(uint64_t handle, LoaderCancellation cancellation) {
  LoaderLocks::Session session(locks, cancellation);
  LoaderLocks::Guard mutation(session, LoaderLocks::Kind::Mutation);
  if (mutation.error())
    return -mutation.error();
  // Keep failed private cleanup owned by this service until it can be retried.
  long error = closing.abort();
  if (error)
    return -error;
  error = scopes.prepare_close(handle, closing);
  if (!error)
    error = scopes.publish(closing);
  if (error) {
    long cleanup = closing.abort();
    return cleanup ? -cleanup : -error;
  }
  // FIXME: Residency, including dependency cycles, TLS and unwind records,
  // is unconditional until coordinated unloading exists. Do not finalize here.
  // Publication transferred the arena; only the empty transaction is replaced.
  closing.~Transaction();
  new (&closing) RuntimeScopes::Transaction(page);
  return 0;
}

LoaderAPI RuntimeAPI::services() {
  return {this,
          [](void *context, const char *name, int flags, uintptr_t pc,
             LoaderCancellation cancellation, uint64_t *out) {
            return static_cast<RuntimeAPI *>(context)->open(name, flags, pc,
                                                            cancellation, out);
          },
          [](void *context, uint64_t handle, const char *name, uintptr_t pc,
             LoaderCancellation cancellation, uintptr_t *out) {
            return static_cast<RuntimeAPI *>(context)->lookup(
                handle, name, pc, cancellation, out);
          },
          [](void *context, uint64_t handle, LoaderCancellation cancellation) {
            return static_cast<RuntimeAPI *>(context)->close(handle, cancellation);
          }};
}

long RuntimeAPI::refresh_queries() {
  const auto *view = scopes.snapshot();
  if (!view)
    return EINVAL;
  long error = queries.prepare(*view->registry);
  if (error)
    return -error;
  queries.publish();
  return 0;
}

long RuntimeAPI::address(uintptr_t addr, void *out, LoaderCancellation hooks) {
  if (!out)
    return EINVAL;
  LoaderLocks::Session session(locks, hooks);
  LoaderLocks::Guard mutation(session, LoaderLocks::Kind::Mutation);
  if (mutation.error())
    return -mutation.error();
  if (long error = refresh_queries())
    return error;
  return RuntimeQueries::address(*queries.snapshot(), addr,
                                 *static_cast<Dl_info *>(out));
}

long RuntimeAPI::info(uint64_t token, int request, void *out,
                      LoaderCancellation hooks) {
  if (!out)
    return EINVAL;
  LoaderLocks::Session session(locks, hooks);
  LoaderLocks::Guard mutation(session, LoaderLocks::Kind::Mutation);
  if (mutation.error())
    return -mutation.error();
  const auto *view = scopes.snapshot();
  const ImageRegistry::Image *image = nullptr;
  if (view)
    for (const auto &handle : view->handles)
      if (handle.token == token) {
        image = handle.global_namespace ? view->registry->images[0]
                                        : handle.local->images[0];
        break;
      }
  if (!image || (request != InfoLinkMap && request != InfoTLSModid &&
                 request != InfoTLSData))
    return EINVAL;
  if (long error = refresh_queries())
    return error;
  const auto *record = RuntimeQueries::record(*queries.snapshot(), image);
  if (!record)
    return EINVAL;
  if (request == InfoLinkMap)
    *static_cast<link_map **>(out) = const_cast<link_map *>(&record->map);
  else if (request == InfoTLSModid)
    *static_cast<size_t *>(out) =
        image->elf.tls_header() ? image->tls.module_id : 0;
  else {
    if (image->elf.tls_header() && !peek_tls)
      return ENOSYS;
    *static_cast<void **>(out) =
        image->elf.tls_header() ? peek_tls(tls.context, &image->tls) : nullptr;
  }
  return 0;
}

long RuntimeAPI::iterate(int (*callback)(void *, void *), void *argument,
                         LoaderCancellation hooks, int *result) {
  if (!callback || !result)
    return EINVAL;
  *result = 0;
  const RuntimeQueries::Snapshot *view;
  {
    LoaderLocks::Session session(locks, hooks);
    LoaderLocks::Guard mutation(session, LoaderLocks::Kind::Mutation);
    if (mutation.error())
      return -mutation.error();
    if (long error = refresh_queries())
      return error;
    view = queries.snapshot();
  }
  // Residency pins the immutable list. No loader lock spans application code.
  for (const auto &record : view->records) {
    dl_phdr_info info;
    if (long error = RuntimeQueries::phdr_info(*view, record, tls.context,
                                               peek_tls, info))
      return error;
    *result = callback(&info, argument);
    if (*result)
      break;
  }
  return 0;
}

LoaderProcessAPI RuntimeAPI::process_services() {
  return {this,
          [](void *p) {
            return -static_cast<RuntimeAPI *>(p)->locks.prepare_fork();
          },
          [](void *p, bool child) {
            static_cast<RuntimeAPI *>(p)->locks.finish_fork(child);
          }};
}

LoaderQueryAPI RuntimeAPI::query_services() {
  return {this,
          [](void *p, uintptr_t a, void *out, LoaderCancellation c) {
            return static_cast<RuntimeAPI *>(p)->address(a, out, c);
          },
          [](void *p, uint64_t h, int r, void *out, LoaderCancellation c) {
            return static_cast<RuntimeAPI *>(p)->info(h, r, out, c);
          },
          [](void *p, int (*f)(void *, void *), void *a, LoaderCancellation c,
             int *out) {
            return static_cast<RuntimeAPI *>(p)->iterate(f, a, c, out);
          }};
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
