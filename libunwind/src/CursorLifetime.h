//===-- Internal cursor lifecycle hooks --------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBUNWIND_CURSOR_LIFETIME_H
#define LIBUNWIND_CURSOR_LIFETIME_H

#if defined(_LIBUNWIND_MMIX_STARTUP_IMAGES) ||                                  \
    defined(_LIBUNWIND_MMIX_CURSOR_LIFETIME)
#include "mmix/CursorLifetime.h"
#define _LIBUNWIND_CURSOR_SCOPE(cursor)                                        \
  struct MMIXCursorLifetime cursor##_lifetime                                  \
      __attribute__((cleanup(__libunwind_mmix_cursor_end))) = {0, 0, 0, 0};    \
  __libunwind_mmix_cursor_begin(&cursor##_lifetime, &(cursor))
#define _LIBUNWIND_CURSOR_ESCAPE(cursor) __libunwind_mmix_cursor_escape(cursor)
#define _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(cursor, stop)                        \
  struct MMIXCursorLifetime cursor##_lifetime                                  \
      __attribute__((cleanup(__libunwind_mmix_cursor_end))) = {0, 0, 0, 0};    \
  __libunwind_mmix_cursor_begin_with_stop(&cursor##_lifetime, &(cursor),       \
                                          (uintptr_t)(stop))
#define _LIBUNWIND_CURSOR_FINISH(cursor) __libunwind_mmix_cursor_finish(cursor)
#define _LIBUNWIND_CURSOR_RESET(cursor) __libunwind_mmix_cursor_reset(cursor)
#define _LIBUNWIND_CURSOR_COPY(to, from) __libunwind_mmix_cursor_copy(to, from)
#define _LIBUNWIND_FORCED_RESUME(exception, stop, argument)                      \
  __libunwind_mmix_forced_resume(exception, (uintptr_t)(stop), argument)
#define _LIBUNWIND_FORCED_ENTER(cursor, exception, stop, argument)               \
  __libunwind_mmix_forced_enter(cursor, exception, (uintptr_t)(stop), argument)
#define _LIBUNWIND_FORCED_STOP(cursor, exception, stop, argument, entering)      \
  __libunwind_mmix_forced_stop(cursor, exception, (uintptr_t)(stop), argument,   \
                             entering)
#define _LIBUNWIND_FORCED_LANDING(cursor, exception, stop, argument)             \
  __libunwind_mmix_forced_landing(cursor, exception, (uintptr_t)(stop), argument)
#define _LIBUNWIND_FORCED_ABORT(exception, stop, argument)                       \
  __libunwind_mmix_forced_abort(exception, (uintptr_t)(stop), argument)
#else
#define _LIBUNWIND_CURSOR_SCOPE(cursor)
#define _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(cursor, stop)
#define _LIBUNWIND_CURSOR_FINISH(cursor) ((void)0)
#define _LIBUNWIND_CURSOR_ESCAPE(cursor) ((void)0)
#define _LIBUNWIND_CURSOR_RESET(cursor) ((void)0)
#define _LIBUNWIND_CURSOR_COPY(to, from) 1
#define _LIBUNWIND_FORCED_RESUME(exception, stop, argument) 1
#define _LIBUNWIND_FORCED_ENTER(cursor, exception, stop, argument) 1
#define _LIBUNWIND_FORCED_STOP(cursor, exception, stop, argument, entering) 1
#define _LIBUNWIND_FORCED_LANDING(cursor, exception, stop, argument) 1
#define _LIBUNWIND_FORCED_ABORT(exception, stop, argument) ((void)0)
#endif
#endif
