//===-- MMIX private cursor ownership -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CursorLifetime.h"

namespace {
// Libunwind is a startup provider. Lookup must not enter a dynamic TLS allocator.
thread_local MMIXCursorLifetime *owners __attribute__((tls_model("initial-exec")));
thread_local void *query __attribute__((tls_model("initial-exec")));
thread_local uintptr_t managed_stop __attribute__((tls_model("initial-exec")));

MMIXCursorLifetime *find(const void *cursor) {
  for (auto *owner = owners; owner; owner = owner->previous)
    if (owner->cursor == cursor)
      return owner;
  return nullptr;
}
void release(MMIXCursorLifetime &owner, bool escape) {
  if (owner.token) {
    bool ok = escape ? __libunwind_mmix_pin_image(owner.token)
                     : __libunwind_mmix_drop_image(owner.token);
    if (!ok)
      __builtin_trap();
    owner.token = 0;
  }
}
void remove(MMIXCursorLifetime &owner) {
  auto **link = &owners;
  while (*link && *link != &owner)
    link = &(*link)->previous;
  if (!*link)
    __builtin_trap();
  *link = owner.previous;
  owner = {};
}
} // namespace

extern "C" __attribute__((visibility("protected"))) void
__libunwind_mmix_cursor_begin(MMIXCursorLifetime *owner, void *cursor) {
  if (!owner || !cursor || find(cursor))
    __builtin_trap();
  *owner = {cursor, 0, owners};
  owners = owner;
}
extern "C" __attribute__((visibility("protected"))) void
__libunwind_mmix_set_managed_stop(uintptr_t stop) {
  if (!stop || (managed_stop && managed_stop != stop))
    __builtin_trap();
  managed_stop = stop;
}
extern "C" void
__libunwind_mmix_cursor_begin_with_stop(MMIXCursorLifetime *owner, void *cursor,
                                        uintptr_t stop) {
  // Public stop callbacks may longjmp without releasing a private owner.
  // Only the libc root protocol promises an explicit terminal finish hook.
  if (!stop || stop == managed_stop)
    __libunwind_mmix_cursor_begin(owner, cursor);
  else
    *owner = {};
}
extern "C" __attribute__((visibility("protected"))) void
__libunwind_mmix_cursor_end(MMIXCursorLifetime *owner) {
  if (owner->cursor) {
    release(*owner, false);
    remove(*owner);
  }
}
extern "C" __attribute__((visibility("protected"))) void
__libunwind_mmix_cursor_finish(void *cursor) {
  if (auto *owner = find(cursor))
    __libunwind_mmix_cursor_end(owner);
}
extern "C" void __libunwind_mmix_cursor_escape(void *cursor) {
  if (auto *owner = find(cursor)) {
    // A raw resume/escaping metadata pointer has no observable final owner.
    // Keep its generation resident, but consume the finite query slot. Never
    // leave a TLS link to the stack frame that jumpto is about to discard.
    release(*owner, true);
    remove(*owner);
  }
}
extern "C" void __libunwind_mmix_cursor_reset(void *cursor) {
  if (auto *owner = find(cursor))
    release(*owner, false);
}
extern "C" int __libunwind_mmix_cursor_copy(void *to, const void *from) {
  auto *source = find(from);
  auto *destination = find(to);
  if (!source || !destination || source == destination)
    return 0;
  MMIXUnwindImage copy{};
  if (source->token && !__libunwind_mmix_retain_image(source->token, &copy))
    return 0;
  release(*destination, false);
  destination->token = copy.token;
  return 1;
}
extern "C" void *__libunwind_mmix_cursor_query_enter(void *cursor) {
  void *previous = query;
  query = cursor;
  return previous;
}
extern "C" void __libunwind_mmix_cursor_query_leave(void *previous) {
  query = previous;
}
bool mmixFindCursorImage(uintptr_t pc, MMIXUnwindImage &view, bool &owned) {
  auto *owner = query ? find(query) : nullptr;
  owned = owner != nullptr;
  if (!owner)
    return __libunwind_mmix_find_startup_image(pc, &view);
  if (owner->token)
    return __libunwind_mmix_lookup_image(owner->token, pc, &view);
  if (!__libunwind_mmix_acquire_image(pc, &view))
    return false;
  owner->token = view.token;
  return true;
}
