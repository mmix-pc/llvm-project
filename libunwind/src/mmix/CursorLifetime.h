//===-- MMIX private cursor ownership ----------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBUNWIND_MMIX_CURSOR_LIFETIME_H
#define LIBUNWIND_MMIX_CURSOR_LIFETIME_H

#include <stdint.h>

// Stack-owned sidecar. This is not part of unw_cursor_t or unw_context_t.
// An internal cursor is registered before initialization and released on scope
// exit. Raw public copies remain outside this protocol and use resident views.
struct MMIXCursorLifetime {
  void *cursor;
  uintptr_t token;
  struct MMIXCursorLifetime *previous;
};

#ifdef __cplusplus
extern "C" {
#endif
// Imported by libc as well as used internally; protection belongs to the
// provider definitions, not to declarations in another shared object.
void __libunwind_mmix_cursor_begin(struct MMIXCursorLifetime *, void *);
void __libunwind_mmix_cursor_end(struct MMIXCursorLifetime *);
void __libunwind_mmix_cursor_finish(void *);
void __libunwind_mmix_set_managed_stop(uintptr_t);
__attribute__((visibility("hidden"))) void
__libunwind_mmix_cursor_begin_with_stop(struct MMIXCursorLifetime *, void *,
                                        uintptr_t);
__attribute__((visibility("hidden"))) void
__libunwind_mmix_cursor_escape(void *);
__attribute__((visibility("hidden"))) int
__libunwind_mmix_cursor_copy(void *, const void *);
__attribute__((visibility("hidden"))) void
__libunwind_mmix_cursor_reset(void *);
__attribute__((visibility("hidden"))) void *
__libunwind_mmix_cursor_query_enter(void *);
__attribute__((visibility("hidden"))) void
__libunwind_mmix_cursor_query_leave(void *);
#ifdef __cplusplus
}
#include "StartupImages.h"
bool mmixFindCursorImage(uintptr_t, MMIXUnwindImage &, bool &owned);
class MMIXCursorQuery {
  void *previous;

public:
  explicit MMIXCursorQuery(void *cursor)
      : previous(__libunwind_mmix_cursor_query_enter(cursor)) {}
  MMIXCursorQuery(const MMIXCursorQuery &) = delete;
  MMIXCursorQuery &operator=(const MMIXCursorQuery &) = delete;
  ~MMIXCursorQuery() { __libunwind_mmix_cursor_query_leave(previous); }
};
#endif
#endif
