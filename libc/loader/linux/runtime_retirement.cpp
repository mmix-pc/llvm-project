//===-- Deferred loader retirement coordinator ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/runtime_retirement.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

long RuntimeRetirement::roots(cpp::span<const Image *const> &out) {
  out = {};
  const auto *view = scopes.snapshot();
  if (!view || view->handles.size() > __SIZE_MAX__ / sizeof(Image *))
    return -EINVAL;
  if (view->handles.empty())
    return 0;
  void *memory;
  long error = scratch.allocate(view->handles.size() * sizeof(Image *),
                                alignof(Image *), memory);
  if (error)
    return error;
  auto **entries = static_cast<const Image **>(memory);
  size_t count = 0;
  for (const auto &handle : view->handles)
    if (!handle.global_namespace && !handle.local->images.empty())
      entries[count++] = handle.local->images[0];
  out = {entries, count};
  return 0;
}

long RuntimeRetirement::close(uint64_t handle, LoaderLocks::Session &session) {
  if (!session.callback_owner())
    return -EINVAL;
  long error = closing.abort();
  if (!error)
    error = scopes.prepare_close(handle, closing);
  if (!error)
    error = scopes.prepare_unwind(closing);
  if (!error)
    error = scopes.publish(closing);
  if (error) {
    long cleanup = closing.abort();
    if (cleanup)
      events.request();
    return cleanup ? cleanup : error;
  }
  closing.~Transaction();
  new (&closing) RuntimeScopes::Transaction(page);
  events.request();
  return 0;
}

long RuntimeRetirement::request_exit(LoaderLocks::Session &session) {
  if (!session.callback_owner())
    return -EINVAL;
  exiting = true;
  events.request();
  return 0;
}

long RuntimeRetirement::prepare_removal(LoaderLocks::Session &session) {
  if (working) {
    long error = working->scope.abort();
    if (!error)
      error = working->arena.reset();
    if (error)
      return error;
    working->images = {};
  }
  long error = queries.abort();
  if (!error)
    error = scratch.reset();
  cpp::span<const Image *const> external, candidates;
  if (!error)
    error = roots(external);
  if (!error)
    error = initializers.retirement_candidates(
        *scopes.snapshot()->registry, scratch, external, candidates, session);
  if (error || candidates.empty())
    return error;
  if (!working) {
    void *memory;
    error = controls.allocate(sizeof(Removal), alignof(Removal), memory);
    if (error)
      return error;
    working = new (memory) Removal(page);
  }
  void *memory;
  error = working->arena.allocate(candidates.size() * sizeof(Image *),
                                  alignof(Image *), memory);
  if (error)
    return error;
  auto **images = static_cast<const Image **>(memory);
  for (size_t i = 0; i < candidates.size(); ++i)
    images[i] = candidates[i];
  working->images = {images, candidates.size()};
  error = scopes.prepare_retire(working->images, working->scope);
  if (!error)
    error = scopes.prepare_unwind(working->scope);
  if (!error)
    error = queries.prepare(*working->scope.view()->registry);
  if (!error)
    error = scopes.publish(working->scope);
  if (error)
    return error;
  queries
      .publish(); // Prepared publication cannot fail or call application code.
  working->state = Removal::WaitingReaders;
  working->next = removed;
  removed = working;
  working = nullptr;
  return 0;
}

long RuntimeRetirement::advance_readers() {
  for (auto *entry = removed; entry; entry = entry->next) {
    if (entry->state != Removal::WaitingReaders)
      continue;
    bool drained = true;
    for (const auto *image : entry->images)
      drained &=
          scopes.readers_drained(image) && queries.readers_drained(image);
    if (drained)
      entry->state = Removal::Ready;
  }
  long scope_error = scopes.collect_snapshots();
  long query_error = queries.collect_snapshots();
  if (scope_error && scope_error != -EBUSY)
    return scope_error;
  return query_error == -EBUSY ? 0 : query_error;
}

long RuntimeRetirement::drain(LoaderLocks::Session &session,
                              RuntimeInitializers::Finalizer invoke) {
  if (!session.unlocked_callback_owner())
    return -EINVAL;
  {
    LoaderLocks::Guard guard(session, LoaderLocks::Kind::Mutation);
    if (guard.error())
      return guard.error();
    if (active) {
      events.request();
      return 0;
    }
    active = true;
  }
  long error = 0;
  while (!error) {
    bool process_exit = false;
    {
      LoaderLocks::Guard guard(session, LoaderLocks::Kind::Mutation);
      if (guard.error())
        __builtin_trap();
      if (!events.take()) {
        active = false;
        return 0;
      }
      process_exit = exiting;
      if (!process_exit) {
        error = closing.abort();
        if (!error)
          error = plan.reset();
        if (!error)
          error = scratch.reset();
        cpp::span<const Image *const> external;
        if (!error)
          error = roots(external);
        if (!error)
          error = initializers.collect_finalizers(
              *scopes.snapshot()->registry, plan, session, false, external);
      }
    }
    if (process_exit) {
      error = initializers.finalize_process_exit(session, invoke);
      LoaderLocks::Guard guard(session, LoaderLocks::Kind::Mutation);
      if (guard.error())
        __builtin_trap();
      active = false;
      if (!error)
        events.take();
      else
        events.request();
      return error;
    }
    if (!error)
      error = initializers.invoke_finalizers(plan, session, invoke);
    {
      LoaderLocks::Guard guard(session, LoaderLocks::Kind::Mutation);
      if (guard.error())
        __builtin_trap();
      if (!error)
        error = initializers.complete_finalizers(plan, session);
      if (!error && !exiting)
        error = prepare_removal(session);
      if (!error && !exiting)
        error = advance_readers();
      if (error) {
        active = false;
        events.request();
      }
    }
  }
  return error;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
