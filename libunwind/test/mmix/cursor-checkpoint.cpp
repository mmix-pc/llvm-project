//===-- Native MMIX cursor checkpoint tests -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Production ownership helper with a deterministic image-service substitute.
// This is not real loader reclamation, stack unwinding or target execution.
#include "mmix/CursorLifetime.h"
#include <cassert>
#include <pthread.h>

static unsigned acquires, drops, pins;
extern "C" bool __libunwind_mmix_find_startup_image(uintptr_t,
                                                    MMIXUnwindImage *) {
  assert(false && "raw resident queries are not managed disposal");
  return false;
}
extern "C" bool __libunwind_mmix_acquire_image(uintptr_t pc,
                                               MMIXUnwindImage *view) {
  ++acquires;
  *view = {};
  view->token = pc;
  return true;
}
extern "C" bool __libunwind_mmix_drop_image(uintptr_t token) {
  assert(token == 101 || token == 102);
  ++drops;
  return true;
}
extern "C" bool __libunwind_mmix_pin_image(uintptr_t) {
  ++pins;
  return true;
}
extern "C" bool __libunwind_mmix_retain_image(uintptr_t, MMIXUnwindImage *) {
  assert(false);
  return false;
}
extern "C" bool __libunwind_mmix_lookup_image(uintptr_t, uintptr_t,
                                              MMIXUnwindImage *) {
  assert(false);
  return false;
}

static void own(void *cursor, uintptr_t pc) {
  MMIXCursorQuery query(cursor);
  MMIXUnwindImage view;
  bool owned = false;
  assert(mmixFindCursorImage(pc, view, owned) && owned && view.token == pc);
}

static void *foreign(void *context) {
  auto *checkpoint = static_cast<MMIXCursorCheckpoint *>(context);
  int cursor;
  MMIXCursorLifetime owner;
  __libunwind_mmix_cursor_begin(&owner, &cursor);
  assert(!__libunwind_mmix_cursor_at_checkpoint(checkpoint));
  assert(!__libunwind_mmix_cursor_finish_at(&cursor, checkpoint));
  assert(owner.cursor == &cursor);
  __libunwind_mmix_cursor_end(&owner);
  return nullptr;
}

int main() {
  MMIXCursorCheckpoint empty, parent;
  __libunwind_mmix_cursor_checkpoint(&empty);
  assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
  int a, b, c;
  MMIXCursorLifetime outer, inner, extra;
  __libunwind_mmix_cursor_begin(&outer, &a);
  own(&a, 101);
  void *before = __libunwind_mmix_cursor_query_enter(&a);
  __libunwind_mmix_cursor_checkpoint(&parent);
  assert(!__libunwind_mmix_cursor_finish_at(&a, &parent));
  assert(outer.token == 101 && drops == 0);
  __libunwind_mmix_cursor_begin(&inner, &b);
  own(&b, 102);
  auto previous = __libunwind_mmix_cursor_query_enter(&b);
  assert(!__libunwind_mmix_cursor_finish_at(&b, &parent));
  assert(inner.token == 102 && drops == 0);
  __libunwind_mmix_cursor_query_leave(previous);
  __libunwind_mmix_cursor_begin(&extra, &c);
  assert(!__libunwind_mmix_cursor_finish_at(&b, &parent));
  assert(!__libunwind_mmix_cursor_finish_at(&c, &parent));
  __libunwind_mmix_cursor_end(&extra);
  assert(!__libunwind_mmix_cursor_finish_at(&c, &parent));
  assert(!__libunwind_mmix_cursor_finish_at(&b, nullptr));
  auto bad = parent;
  bad.thread = 0;
  assert(!__libunwind_mmix_cursor_finish_at(&b, &bad));
  bad = parent;
  bad.owners = &inner;
  assert(!__libunwind_mmix_cursor_finish_at(&b, &bad));
  assert(drops == 0 && acquires == 2 && pins == 0);
  assert(__libunwind_mmix_cursor_finish_at(&b, &parent));
  assert(!inner.cursor && !inner.token && !inner.previous);
  assert(drops == 1 && pins == 0 && outer.token == 101);
  assert(__libunwind_mmix_cursor_at_checkpoint(&parent));
  assert(!__libunwind_mmix_cursor_finish_at(&b, &parent));
  pthread_t thread;
  assert(pthread_create(&thread, nullptr, foreign, &parent) == 0);
  assert(pthread_join(thread, nullptr) == 0);
  assert(__libunwind_mmix_cursor_at_checkpoint(&parent));
  __libunwind_mmix_cursor_query_leave(before);
  assert(__libunwind_mmix_cursor_finish_at(&a, &empty));
  assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
  assert(drops == 2 && pins == 0);
}
