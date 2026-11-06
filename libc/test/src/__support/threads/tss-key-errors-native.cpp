//===-- Native POSIX251 TSS key error regression ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Include before thread.h's avoid-host-new machinery. No host <new> is used.
#include "src/__support/CPP/new.h"
#include "src/__support/libc_errno.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <threads.h>

// Some host pthread headers do not provide libc's destructor typedef.
using __pthread_tss_dtor_t = void (*)(void *);

#ifdef TSS_TEST_SHARED
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#include "src/__support/threads/linux/mmix/loader_cancellation.h"
#include "src/stdlib/free.h"
#include "src/stdlib/malloc.h"

namespace {
struct Allocation {
  void *storage;
  uint64_t pin;
};
Allocation allocations[4096];
bool pins[4096];
unsigned depth, enters, leaves, retains, releases, attempts, allocated, freed;
unsigned live_pins, live_allocations;
uint64_t last_pin, pending_pin;
int retain_error;
bool fail_allocation;

long enter(uint64_t *saved) {
  assert(depth == 0);
  *saved = depth++;
  pending_pin = 0;
  ++enters;
  libc_errno = EBUSY;
  return 0;
}

long leave(uint64_t saved) {
  assert(depth == 1 && saved == 0);
  --depth;
  ++leaves;
  libc_errno = EINTR;
  return 0;
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
loader::LoaderCancellation loader_cancellation_hooks() {
  return {enter, leave, nullptr, nullptr, nullptr, nullptr};
}

long retain_shared_tls_destructor(void *dso, uintptr_t callback,
                                  uint64_t *pin) {
  assert(depth == 1 && !dso && callback && !*pin);
  ++retains;
  libc_errno = EIO;
  if (retain_error)
    return retain_error;
  assert(last_pin + 1 < 4096);
  *pin = pending_pin = ++last_pin;
  pins[*pin] = true;
  ++live_pins;
  return 0;
}

long release_shared_tls_destructor(uint64_t pin) {
  assert(depth == 1 && pin && pin <= last_pin && pins[pin]);
  pins[pin] = false;
  --live_pins;
  ++releases;
  libc_errno = ERANGE;
  return 0;
}
} // namespace mmix

void *malloc(size_t size) {
  assert(depth == 1);
  assert(!pending_pin || pins[pending_pin]);
  ++attempts;
  libc_errno = EIO; // ENOMEM must come from the registry, not this value.
  if (fail_allocation)
    return nullptr;
  assert(allocated < 4096);
  void *storage = ::malloc(size);
  assert(storage);
  allocations[allocated++] = {storage, pending_pin};
  ++live_allocations;
  return storage;
}

void free(void *storage) {
  assert(depth == 1 && storage);
  for (unsigned i = 0; i < allocated; ++i) {
    auto &allocation = allocations[i];
    if (allocation.storage != storage)
      continue;
    // The exact ticket must be released before freeing its registration.
    assert(!allocation.pin || !pins[allocation.pin]);
    allocation.storage = nullptr;
    --live_allocations;
    ++freed;
    ::free(storage);
    libc_errno = EFAULT;
    return;
  }
  assert(false && "unknown or double free");
}
} // namespace LIBC_NAMESPACE_DECL

#include "src/__support/threads/linux/mmix/tss.cpp"
#else
#include "src/__support/threads/tss.cpp"
#endif
#include "src/pthread/pthread_key_create.cpp"
#include "src/threads/tss_create.cpp"
#include "src/errno/libc_errno.cpp"

namespace L = LIBC_NAMESPACE;
static constexpr unsigned Sentinel = 0x7fffffff;
static unsigned callbacks;
static int payload;

static void callback(void *value) {
  assert(value == &payload);
  ++callbacks;
}

// Exercise the actual wrappers, not a reimplementation of their mapping.
// Only pthread promises errno preservation; C threads maps every error to
// thrd_error, irrespective of errno and of the internal error number.
static unsigned create(unsigned api, int error, L::TSSDtor *dtor) {
  libc_errno = EDOM;
  unsigned result = Sentinel;
  if (api == 0) {
    auto key = L::new_tss_key(dtor);
    assert(bool(key) == (error == 0));
    if (error)
      assert(key.error() == error);
    else
      result = *key;
  } else if (api == 1) {
    pthread_key_t key = Sentinel;
    assert(L::pthread_key_create(&key, dtor) == error);
    assert(int(libc_errno) == EDOM);
    result = key;
  } else {
    tss_t key = Sentinel;
    assert(L::tss_create(&key, dtor) ==
           (error ? thrd_error : thrd_success));
    result = key;
  }
  if (error)
    assert(result == Sentinel);
  else
    assert(result < 1024);
#ifdef TSS_TEST_SHARED
  assert(depth == 0 && enters == leaves);
#endif
  return result;
}

static void failures(int error, bool has_callback = true) {
  unsigned before_callbacks = callbacks;
  for (unsigned api = 0; api != 3; ++api) {
#ifdef TSS_TEST_SHARED
    unsigned old_retains = retains, old_releases = releases;
    unsigned old_attempts = attempts, old_allocated = allocated;
    unsigned old_freed = freed, old_pins = live_pins;
    unsigned old_live = live_allocations, old_enters = enters;
#endif
    create(api, error, has_callback ? callback : nullptr);
    assert(callbacks == before_callbacks);
#ifdef TSS_TEST_SHARED
    bool retained = has_callback && !retain_error;
    bool tried = !has_callback || !retain_error;
    bool obtained = tried && !fail_allocation;
    assert(retains == old_retains + unsigned(has_callback));
    assert(releases == old_releases + unsigned(retained));
    assert(attempts == old_attempts + unsigned(tried));
    assert(allocated == old_allocated + unsigned(obtained));
    assert(freed == old_freed + unsigned(obtained));
    assert(live_pins == old_pins && live_allocations == old_live);
    assert(enters == old_enters + 1);
#endif
  }
}

int main() {
#ifdef TSS_TEST_SHARED
  // Keep an accepted registration live while each rollback path is exercised.
  unsigned survivor = create(1, 0, callback);
  assert(L::set_tss_value(survivor, &payload));
  retain_error = EACCES;
  failures(EACCES);
  retain_error = EINVAL;
  failures(EINVAL);
  retain_error = 0;
  fail_allocation = true;
  failures(ENOMEM);
  failures(ENOMEM, false);
  fail_allocation = false;
  assert(live_pins == 1 && live_allocations == 1);
  assert(L::get_tss_value(survivor) == &payload);
  L::internal::call_tss_destructors();
  assert(callbacks == 1);
  assert(L::tss_key_delete(survivor));
#endif

  unsigned keys[1024];
  bool seen[1024] = {};
  for (unsigned i = 0; i != 1024; ++i) {
    keys[i] = create(i % 3, 0, i % 2 ? callback : nullptr);
    assert(!seen[keys[i]]);
    seen[keys[i]] = true;
  }
  failures(EAGAIN);
  failures(EAGAIN, false);

  // A full registry must recover exactly one slot, including null destructors.
  // A deleted generation's payload must not reach its replacement callback.
  unsigned before_callbacks = callbacks;
  for (unsigned api = 0; api != 3; ++api) {
    unsigned slot = 19 + api;
    unsigned old_key = keys[slot];
    assert(L::set_tss_value(old_key, &payload));
    assert(L::tss_key_delete(old_key));
    keys[slot] = create(api, 0, callback);
    assert(keys[slot] == old_key);
    assert(L::get_tss_value(keys[slot]) == nullptr);
    failures(EAGAIN);
  }
  L::internal::call_tss_destructors();
  assert(callbacks == before_callbacks);
  assert(L::set_tss_value(keys[19], &payload));
  L::internal::call_tss_destructors();
  assert(callbacks == before_callbacks + 1);
  for (unsigned key : keys)
    assert(L::tss_key_delete(key));
  L::internal::call_tss_destructors();
  assert(callbacks == before_callbacks + 1);
#ifdef TSS_TEST_SHARED
  assert(!depth && enters == leaves);
  assert(!live_pins && !live_allocations && allocated == freed);
  assert(releases == last_pin);
  for (uint64_t pin = 1; pin <= last_pin; ++pin)
    assert(!pins[pin]);
#endif
}
