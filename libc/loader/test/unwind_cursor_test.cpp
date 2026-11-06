//===-- MMIX cursor lifetime tests ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define _LIBUNWIND_MMIX_STARTUP_IMAGES
#include "../../../libunwind/src/CursorLifetime.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <sched.h>

static void check(bool condition) {
  if (!condition)
    std::abort();
}
static struct {
  alignas(8) char phdr[56];
  char index[16];
} image;
static MMIXUnwindReadableRange range{reinterpret_cast<uintptr_t>(&image),
                                     sizeof(image)};
static MMIXUnwindImage view{0,
                            reinterpret_cast<uintptr_t>(image.phdr),
                            1,
                            reinterpret_cast<uintptr_t>(image.index),
                            sizeof(image.index),
                            reinterpret_cast<uintptr_t>(&range),
                            1,
                            1};
static bool live[128];
static uintptr_t next = 1;
static unsigned references, pins, resident;
static bool retired, fail_copy;
static unsigned retain_calls, fail_retain_call;
static long find(void *, uintptr_t pc, MMIXUnwindImage *out) {
  if (pc != 100 || retired)
    return 2;
  *out = view;
  ++resident;
  return 0;
}
static void release(void *, uintptr_t) {}
static long acquire(void *, uintptr_t pc, MMIXUnwindImage *out) {
  if (pc != 100 || retired)
    return 2;
  *out = view;
  out->token = next++;
  live[out->token] = true;
  ++references;
  return 0;
}
static long retain(void *, uintptr_t token, MMIXUnwindImage *out) {
  ++retain_calls;
  if (!token || token >= next || !live[token] || fail_copy ||
      (fail_retain_call && retain_calls == fail_retain_call))
    return 22;
  *out = view;
  out->token = next++;
  live[out->token] = true;
  ++references;
  return 0;
}
static long drop(void *, uintptr_t token) {
  if (!token || token >= next || !live[token])
    return 22;
  live[token] = false;
  --references;
  return 0;
}
static long lookup(void *, uintptr_t token, uintptr_t pc,
                   MMIXUnwindImage *out) {
  if (!token || token >= next || !live[token])
    return 22;
  if (pc != 100)
    return 2;
  *out = view;
  out->token = token;
  return 0;
}
static long pin(void *p, uintptr_t token) {
  long error = drop(p, token);
  if (!error)
    ++pins;
  return error;
}
static bool query(void *cursor, uintptr_t pc = 100) {
  MMIXCursorQuery scope(cursor);
  MMIXUnwindImage result{};
  bool owned;
  return mmixFindCursorImage(pc, result, owned);
}
// Inject valid primary/fallback leases independently of forced-walk setup.
// The copy transaction itself is the production implementation.
static void copy_partial_retain_rollback() {
  int source, destination;
  _LIBUNWIND_CURSOR_SCOPE(source);
  _LIBUNWIND_CURSOR_SCOPE(destination);
  MMIXUnwindImage lease{};
  check(!acquire(nullptr, 100, &lease));
  source_lifetime.token = lease.token;
  check(!acquire(nullptr, 100, &lease));
  source_lifetime.fallback_token = lease.token;
  check(query(&destination) && references == 3);
  const uintptr_t primary = source_lifetime.token;
  const uintptr_t fallback = source_lifetime.fallback_token;
  const uintptr_t original = destination_lifetime.token;
  for (unsigned failure = 1; failure <= 2; ++failure) {
    const uintptr_t first_new = next;
    retain_calls = 0;
    fail_retain_call = failure;
    check(!__libunwind_mmix_cursor_copy(&destination, &source));
    check(retain_calls == failure && references == 3);
    check(source_lifetime.token == primary &&
          source_lifetime.fallback_token == fallback);
    check(destination_lifetime.token == original &&
          !destination_lifetime.fallback_token);
    check(live[primary] && live[fallback] && live[original]);
    check(next == first_new + failure - 1);
    if (failure == 2)
      check(!live[first_new]); // First retain was rolled back exactly once.
    check(query(&source) && query(&destination) && references == 3);
  }
  fail_retain_call = 0;
  retain_calls = 0;
  check(__libunwind_mmix_cursor_copy(&destination, &source));
  check(retain_calls == 2 && references == 4 && !live[original]);
  check(destination_lifetime.token != primary &&
        destination_lifetime.fallback_token != fallback &&
        destination_lifetime.token != destination_lifetime.fallback_token);
  check(live[destination_lifetime.token] &&
        live[destination_lifetime.fallback_token]);
  __libunwind_mmix_cursor_reset(&source);
  check(!live[primary] && !live[fallback] && references == 2);
  __libunwind_mmix_cursor_reset(&destination);
  check(!references);
}
static std::atomic<bool> ready;
static void *cancelled(void *) {
  int cursor;
  _LIBUNWIND_CURSOR_SCOPE(cursor);
  check(query(&cursor));
  ready.store(true);
  for (;;) {
    pthread_testcancel();
    sched_yield();
  }
}
int main() {
  MMIXStartupUnwindService service{sizeof(service), 1,       &image, find,
                                   release,         acquire, retain, drop,
                                   lookup,          pin};
  check(__libunwind_mmix_adopt_startup_images(&service));
  int raw;
  check(query(&raw) && resident == 1 && !references);
  {
    int cursor, copy;
    _LIBUNWIND_CURSOR_SCOPE(cursor);
    _LIBUNWIND_CURSOR_SCOPE(copy);
    check(query(&cursor) && references == 1);
    check(query(&cursor) && references == 1); // Step/reset keeps generation.
    check(!query(&cursor, 999) && references == 1); // Failed step keeps owner.
    fail_copy = true;
    check(!__libunwind_mmix_cursor_copy(&copy, &cursor) && references == 1);
    fail_copy = false;
    check(__libunwind_mmix_cursor_copy(&copy, &cursor) && references == 2);
    __libunwind_mmix_cursor_reset(&cursor);
    check(references == 1);
    retired = true;
    check(query(&copy) && !query(&cursor));
    retired = false;
    try {
      int nested;
      _LIBUNWIND_CURSOR_SCOPE(nested);
      check(query(&nested) && references == 2);
      throw 1;
    } catch (int) {
      check(references == 1 && query(&copy));
    }
  }
  check(!references);
  {
    int cursor;
    _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(cursor, 123);
    check(query(&cursor) &&
          !references); // Unknown nonlocal stop stays resident.
  }
  {
    __libunwind_mmix_set_managed_stop(456);
    int cursor;
    _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(cursor, 456);
    check(query(&cursor) && references == 1);
    __libunwind_mmix_cursor_finish(&cursor);
    check(!references);
  }
  {
    int cursor;
    _LIBUNWIND_CURSOR_SCOPE(cursor);
    check(query(&cursor));
    __libunwind_mmix_cursor_finish(&cursor); // Terminal forced-exit root.
    check(!references);
  }
  {
    int cursor;
    _LIBUNWIND_CURSOR_SCOPE(cursor);
    check(query(&cursor));
    __libunwind_mmix_cursor_escape(&cursor); // Nonreturning landing pad.
    check(!references && pins == 1);
    check(query(&cursor) && resident == 3); // No dangling sidecar registration.
  }
  copy_partial_retain_rollback();
  check(!references);
  pthread_t thread;
  check(!pthread_create(&thread, nullptr, cancelled, nullptr));
  while (!ready.load())
    sched_yield();
  check(!pthread_cancel(thread));
  void *result;
  check(!pthread_join(thread, &result) && result == PTHREAD_CANCELED);
  check(!references);
  std::puts("cursor copy, reset, failed step, nested cleanup and host "
            "cancellation passed");
}
