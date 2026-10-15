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
      error = request->load.commit(token);
    if (!error &&
        initializers.attach(request->initialization, *images.snapshot()))
      fatal_startup(); // No fallible work or callback may split commit/attach.
  }
  if (error) {
    long cleanup = request->reset();
    return cleanup ? -cleanup : -error;
  }
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
          }};
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
