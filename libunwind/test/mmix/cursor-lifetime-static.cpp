//===-- Native MMIX static cursor lifetime tests ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Exercise the production lifetime helper without any image service or fake
// lookup. This is not target execution, metadata discovery or forced unwinding.
#include "CursorLifetime.h"
#include <cassert>
#include <pthread.h>

#if !defined(_LIBUNWIND_MMIX_CURSOR_LIFETIME) ||                                \
    defined(_LIBUNWIND_MMIX_STARTUP_IMAGES)
#error This fixture requires the static lifetime opt-in alone
#endif
#ifdef LIBUNWIND_MMIX_STARTUP_IMAGES_H
#error Static lifetime tracking must not import the image provider interface
#endif

static MMIXCursorCheckpoint checkpoint() {
  MMIXCursorCheckpoint result;
  __libunwind_mmix_cursor_checkpoint(&result);
  assert(__libunwind_mmix_cursor_at_checkpoint(&result));
  return result;
}

static void reject(void *cursor, const MMIXCursorCheckpoint *parent) {
  auto before = checkpoint();
  assert(!__libunwind_mmix_cursor_finish_at(cursor, parent));
  assert(__libunwind_mmix_cursor_at_checkpoint(&before));
}

static void cleared(const MMIXCursorLifetime &owner) {
  assert(!owner.cursor && !owner.token && !owner.previous);
}

static void nested_finish() {
  auto empty = checkpoint();
  assert(!empty.owners && !empty.query);
  assert(!__libunwind_mmix_cursor_at_checkpoint(nullptr));
  int outer = 0, raw = 0;
  _LIBUNWIND_CURSOR_SCOPE(outer);
  assert(outer_lifetime.cursor == &outer && !outer_lifetime.token);
  {
    MMIXCursorQuery outerQuery(&outer);
    auto parent = checkpoint();
    assert(parent.owners == &outer_lifetime && parent.query == &outer);
    assert(!__libunwind_mmix_cursor_at_checkpoint(&empty));
    reject(&outer, &parent);
    int child = 0;
    _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(child, 456);
    assert(child_lifetime.cursor == &child && !child_lifetime.token);
    assert(child_lifetime.previous == &outer_lifetime);
    auto childState = checkpoint();
    {
      MMIXCursorQuery childQuery(&child);
      reject(&child, &parent);
      // Even a matching query cannot retire the cursor it is inspecting.
      auto active = parent;
      active.query = &child;
      reject(&child, &active);
      {
        MMIXCursorQuery rawQuery(&raw);
        reject(&child, &parent);
      }
      assert(checkpoint().query == &child);
    }
    assert(__libunwind_mmix_cursor_at_checkpoint(&childState));
    {
      int extra = 0;
      _LIBUNWIND_CURSOR_SCOPE(extra);
      reject(&child, &parent);
      reject(&extra, &parent);
      assert(!extra_lifetime.token);
    }
    assert(__libunwind_mmix_cursor_at_checkpoint(&childState));
    reject(&raw, &parent);
    reject(nullptr, &parent);
    reject(&child, nullptr);
    auto bad = parent;
    bad.thread = 0;
    assert(!__libunwind_mmix_cursor_at_checkpoint(&bad));
    reject(&child, &bad);
    bad = parent;
    bad.owners = &child_lifetime;
    reject(&child, &bad);
    bad = parent;
    bad.query = &raw;
    reject(&child, &bad);
    assert(child_lifetime.cursor == &child && !child_lifetime.token);
    assert(child_lifetime.previous == &outer_lifetime);
    assert(__libunwind_mmix_cursor_finish_at(&child, &parent));
    cleared(child_lifetime);
    assert(__libunwind_mmix_cursor_at_checkpoint(&parent));
    reject(&child, &parent);
    _LIBUNWIND_CURSOR_FINISH(&child);
    assert(__libunwind_mmix_cursor_at_checkpoint(&parent));
  }
  assert(__libunwind_mmix_cursor_finish_at(&outer, &empty));
  cleared(outer_lifetime);
  assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
}

static void copy_reset_escape() {
  auto empty = checkpoint();
  int source = 0, destination = 0, raw = 0;
  _LIBUNWIND_CURSOR_SCOPE(source);
  _LIBUNWIND_CURSOR_SCOPE(destination);
  auto both = checkpoint();
  assert(_LIBUNWIND_CURSOR_COPY(&destination, &source));
  assert(!source_lifetime.token && !destination_lifetime.token);
  assert(!_LIBUNWIND_CURSOR_COPY(&source, &source));
  assert(!_LIBUNWIND_CURSOR_COPY(&destination, &raw));
  assert(!_LIBUNWIND_CURSOR_COPY(&raw, &source));
  assert(!_LIBUNWIND_CURSOR_COPY(&raw, &raw));
  assert(!_LIBUNWIND_CURSOR_COPY(nullptr, &source));
  assert(!_LIBUNWIND_CURSOR_COPY(&destination, nullptr));
  _LIBUNWIND_CURSOR_RESET(&source);
  _LIBUNWIND_CURSOR_RESET(&destination);
  _LIBUNWIND_CURSOR_RESET(&raw);
  _LIBUNWIND_CURSOR_FINISH(&raw);
  _LIBUNWIND_CURSOR_ESCAPE(&raw);
  assert(__libunwind_mmix_cursor_at_checkpoint(&both));
  _LIBUNWIND_CURSOR_ESCAPE(&source);
  cleared(source_lifetime);
  assert(destination_lifetime.cursor == &destination);
  assert(!destination_lifetime.previous && !destination_lifetime.token);
  assert(!_LIBUNWIND_CURSOR_COPY(&destination, &source));
  _LIBUNWIND_CURSOR_FINISH(&destination);
  cleared(destination_lifetime);
  assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
}

static void stop_and_cleanup() {
  auto empty = checkpoint();
  {
    int unmanaged = 0, raw = 0;
    _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(unmanaged, 123);
    cleared(unmanaged_lifetime);
    assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
    assert(!_LIBUNWIND_CURSOR_COPY(&unmanaged, &raw));
    reject(&unmanaged, &empty);
  }
  {
    int noStop = 0;
    _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(noStop, 0);
    assert(noStop_lifetime.cursor == &noStop && !noStop_lifetime.token);
    auto parent = checkpoint();
    try {
      int child = 0;
      _LIBUNWIND_CURSOR_SCOPE(child);
      assert(child_lifetime.previous == &noStop_lifetime);
      throw 1;
    } catch (int) {
      assert(__libunwind_mmix_cursor_at_checkpoint(&parent));
    }
  }
  assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
}

static void *foreign(void *context) {
  auto *other = static_cast<const MMIXCursorCheckpoint *>(context);
  auto empty = checkpoint();
  // Both threads are empty, so rejection must depend on thread identity.
  assert(!other->owners && !other->query);
  assert(!__libunwind_mmix_cursor_at_checkpoint(other));
  int child = 0, unmanaged = 0;
  _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(unmanaged, 456);
  cleared(unmanaged_lifetime); // Managed-stop registration is thread-local.
  _LIBUNWIND_CURSOR_SCOPE(child);
  reject(&child, other);
  assert(child_lifetime.cursor == &child && !child_lifetime.token);
  assert(__libunwind_mmix_cursor_finish_at(&child, &empty));
  cleared(child_lifetime);
  assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
  return nullptr;
}

int main() {
  auto empty = checkpoint();
  __libunwind_mmix_set_managed_stop(456);
  __libunwind_mmix_set_managed_stop(456);
  nested_finish();
  assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
  copy_reset_escape();
  assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
  stop_and_cleanup();
  assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
  pthread_t thread;
  assert(pthread_create(&thread, nullptr, foreign, &empty) == 0);
  assert(pthread_join(thread, nullptr) == 0);
  assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
}
