//===-- Native injection tests for MMIX atexit failures --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/func/aligned_alloc.h"
#include "hdr/func/malloc.h"
#include "src/__support/CPP/new.h"
#include "src/__support/CPP/utility.h"
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#include "src/__support/threads/linux/mmix/loader_cancellation.h"
#include "src/__support/threads/mutex.h"
#include "src/stdlib/_Exit.h"
#include "src/stdlib/atexit.h"
#include "src/stdlib/free.h"
#include "src/stdlib/malloc.h"
#include "test/UnitTest/Test.h"

namespace {
void check(bool ok) {
  if (!ok)
    __builtin_trap();
}

void poison() { __builtin_trap(); }

// State is private to each EXPECT_EXITS child. Tokens are never reused, even
// after rollback, so releasing a different registration cannot pass unnoticed.
struct Pin {
  uintptr_t function;
  void *storage;
  bool live;
  bool invoked;
};
Pin pins[8];
uint64_t last_pin, expected_pins[4];
int depth, live_pins, node_allocations, node_frees, releases, shared_callbacks;
bool fail_pin, fail_node_allocation;

long enter(uint64_t *saved) {
  *saved = depth;
  ++depth;
  return 0;
}

long leave(uint64_t saved) {
  check(depth == int(saved) + 1);
  --depth;
  return 0;
}

int block_attempts, block_allocations;
bool fail_block_allocation;

void *mmix_atexit_failure_block_malloc(size_t size) {
  ++block_attempts;
  if (fail_block_allocation)
    return nullptr;
  void *storage = ::malloc(size);
  check(storage != nullptr);
  ++block_allocations;
  return storage;
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
loader::LoaderCancellation mmix_atexit_failure_cancellation_hooks() {
  return {enter, leave, nullptr, nullptr, nullptr, nullptr};
}

long mmix_atexit_failure_retain(void *dso, uintptr_t function, uint64_t *pin) {
  check(dso == nullptr && function != 0 && depth > 0);
  if (fail_pin)
    return -1;
  check(last_pin + 1 < sizeof(pins) / sizeof(pins[0]));
  *pin = ++last_pin;
  pins[*pin] = {function, nullptr, true, false};
  ++live_pins;
  return 0;
}

long mmix_atexit_failure_retain_process(void *, uintptr_t, uint64_t *) {
  // Ordinary atexit must pin the actual C callback, not use the DSO path.
  __builtin_trap();
}

long mmix_atexit_failure_release(uint64_t pin) {
  check(depth > 0 && pin != 0 && pin <= last_pin);
  Pin &state = pins[pin];
  check(state.live);
  if (state.storage)
    check(state.invoked);
  else
    check(fail_node_allocation);
  state.live = false;
  --live_pins;
  ++releases;
  return 0;
}
} // namespace mmix

void *mmix_atexit_failure_node_malloc(size_t size) {
  check(depth > 0 && last_pin != 0 && pins[last_pin].live);
  check(pins[last_pin].storage == nullptr);
  ++node_allocations;
  if (fail_node_allocation)
    return nullptr;
  void *storage = ::malloc(size);
  check(storage != nullptr);
  pins[last_pin].storage = storage;
  return storage;
}

void mmix_atexit_failure_node_free(void *storage) {
  check(depth > 0 && storage != nullptr);
  for (uint64_t pin = 1; pin <= last_pin; ++pin) {
    Pin &state = pins[pin];
    if (state.storage != storage)
      continue;
    check(!state.live && state.invoked);
    state.storage = nullptr;
    ++node_frees;
    ::free(storage);
    return;
  }
  __builtin_trap();
}

int mmix_atexit_failure_static_atexit(__atexithandler_t);
int mmix_atexit_failure_shared_atexit(__atexithandler_t);
} // namespace LIBC_NAMESPACE_DECL

// Include production allocation/store code under private names. Only this
// AllocChecker's malloc is injected; host/test-framework allocations and the
// production operator delete remain untouched.
#define AllocChecker MMIXAtExitFailureAllocChecker
#define malloc mmix_atexit_failure_block_malloc
#include "src/__support/alloc-checker.h"
#undef malloc
#define BlockStore MMIXAtExitFailureBlockStore
#define ReverseOrderBlockStore MMIXAtExitFailureReverseOrderBlockStore
#include "src/__support/blockstore.h"
#undef BlockStore
#undef AllocChecker

// Select the real public-packaging list, not the unit-test FixedVector. common.h
// is already included, so this does not change entrypoint alias generation.
// Rename helpers and state as well as entrypoints to avoid the linked atexit
// implementation and the host's C-linkage __cxa_atexit/__cxa_finalize.
#define AtExitUnit MMIXAtExitFailureUnit
#define ExitCallbackList MMIXAtExitFailureList
#define handler_list_mtx mmix_atexit_failure_static_mutex
#define stdc_at_exit_func mmix_atexit_failure_stdc_callback
#define call_exit_callbacks mmix_atexit_failure_call_callbacks
#define add_atexit_unit mmix_atexit_failure_add_unit
#ifndef LIBC_COPT_PUBLIC_PACKAGING
#define LIBC_COPT_PUBLIC_PACKAGING
#define MMIX_ATEXIT_FAILURE_UNDEF_PUBLIC_PACKAGING
#endif
#include "src/stdlib/exit_handler.h"
#ifdef MMIX_ATEXIT_FAILURE_UNDEF_PUBLIC_PACKAGING
#undef MMIX_ATEXIT_FAILURE_UNDEF_PUBLIC_PACKAGING
#undef LIBC_COPT_PUBLIC_PACKAGING
#endif
#undef ReverseOrderBlockStore
#define atexit_callbacks mmix_atexit_failure_static_callbacks
#define atexit mmix_atexit_failure_static_atexit
#define __cxa_atexit mmix_atexit_failure_static_cxa_atexit
#define __cxa_finalize mmix_atexit_failure_static_cxa_finalize
#include "src/stdlib/atexit.cpp"
#undef __cxa_finalize
#undef __cxa_atexit
#undef atexit
#undef atexit_callbacks
#undef add_atexit_unit
#undef call_exit_callbacks
#undef handler_list_mtx
#undef ExitCallbackList
#undef AtExitUnit

#define handler_list_mtx mmix_atexit_failure_shared_mutex
#define loader_cancellation_hooks mmix_atexit_failure_cancellation_hooks
#define retain_shared_tls_destructor mmix_atexit_failure_retain
#define retain_shared_process_callback mmix_atexit_failure_retain_process
#define release_shared_tls_destructor mmix_atexit_failure_release
#define malloc mmix_atexit_failure_node_malloc
#define free mmix_atexit_failure_node_free
#define atexit mmix_atexit_failure_shared_atexit
#define __cxa_atexit mmix_atexit_failure_shared_cxa_atexit
#define __cxa_finalize mmix_atexit_failure_shared_cxa_finalize
#include "src/stdlib/linux/mmix/atexit.cpp"
#undef __cxa_finalize
#undef __cxa_atexit
#undef atexit
#undef free
#undef malloc
#undef release_shared_tls_destructor
#undef retain_shared_process_callback
#undef retain_shared_tls_destructor
#undef loader_cancellation_hooks
#undef handler_list_mtx
#undef stdc_at_exit_func

static_assert(LIBC_NAMESPACE::cpp::is_same_v<
              LIBC_NAMESPACE::MMIXAtExitFailureList,
              LIBC_NAMESPACE::MMIXAtExitFailureReverseOrderBlockStore<
                  LIBC_NAMESPACE::MMIXAtExitFailureUnit, 32>>);

namespace {
void observe_shared_callback(__atexithandler_t function) {
  check(depth == 1 && shared_callbacks < 4);
  check(live_pins == 4 - shared_callbacks && node_frees == shared_callbacks);
  // The running callback and every older callback still own their pins. Each
  // newer callback has already released its own pin and freed its own node.
  for (int i = 0; i < 4; ++i) {
    const Pin &state = pins[expected_pins[i]];
    check(state.live == (i >= shared_callbacks));
    check((state.storage != nullptr) == (i >= shared_callbacks));
  }
  Pin &state = pins[expected_pins[shared_callbacks]];
  check(!state.invoked);
  check(state.function == reinterpret_cast<uintptr_t>(function));
  state.invoked = true;
  ++shared_callbacks;
}

void shared_duplicate() { observe_shared_callback(shared_duplicate); }
void shared_middle() { observe_shared_callback(shared_middle); }
void shared_later() { observe_shared_callback(shared_later); }

void shared_failure(bool allocation_failure) {
  check(LIBC_NAMESPACE::mmix_atexit_failure_shared_atexit(shared_duplicate) == 0);
  check(LIBC_NAMESPACE::mmix_atexit_failure_shared_atexit(shared_middle) == 0);
  check(LIBC_NAMESPACE::mmix_atexit_failure_shared_atexit(shared_duplicate) == 0);
  check(depth == 0 && live_pins == 3 && node_allocations == 3);

  fail_pin = !allocation_failure;
  fail_node_allocation = allocation_failure;
  check(LIBC_NAMESPACE::mmix_atexit_failure_shared_atexit(poison) == -1);
  fail_pin = fail_node_allocation = false;
  check(depth == 0 && live_pins == 3 && shared_callbacks == 0);
  check(node_allocations == (allocation_failure ? 4 : 3));
  check(releases == (allocation_failure ? 1 : 0) && node_frees == 0);
  check(last_pin == (allocation_failure ? 4u : 3u));
  for (int i = 1; i <= 3; ++i)
    check(pins[i].live && pins[i].storage && !pins[i].invoked);
  if (allocation_failure)
    check(!pins[4].live && !pins[4].storage && !pins[4].invoked);

  check(LIBC_NAMESPACE::mmix_atexit_failure_shared_atexit(shared_later) == 0);
  check(depth == 0 && live_pins == 4);
  expected_pins[0] = last_pin;
  expected_pins[1] = 3;
  expected_pins[2] = 2;
  expected_pins[3] = 1;
  LIBC_NAMESPACE::mmix_atexit_failure_shared_cxa_finalize(nullptr);
  check(depth == 0 && live_pins == 0 && shared_callbacks == 4);
  check(node_frees == 4 && releases == (allocation_failure ? 5 : 4));
  for (uint64_t pin = 1; pin <= last_pin; ++pin)
    check(!pins[pin].live && pins[pin].storage == nullptr);

  // A rejected callback must not leave a dangling registration, and accepted
  // callbacks must not be replayed or released twice by a subsequent drain.
  LIBC_NAMESPACE::mmix_atexit_failure_shared_cxa_finalize(nullptr);
  check(depth == 0 && live_pins == 0 && shared_callbacks == 4);
  check(node_frees == 4 && releases == (allocation_failure ? 5 : 4));
  LIBC_NAMESPACE::_Exit(0);
}

int static_remaining, static_initial_count;
bool static_later_seen;

template <int Index> void static_callback() {
  check(static_later_seen && static_remaining > 0);
  check(--static_remaining == Index);
}

template <int... Indices>
void register_static_callbacks(
    LIBC_NAMESPACE::cpp::integer_sequence<int, Indices...>) {
  (check(LIBC_NAMESPACE::mmix_atexit_failure_static_atexit(
             static_callback<Indices>) == 0),
   ...);
}

void static_later() {
  check(!static_later_seen && static_remaining == static_initial_count);
  static_later_seen = true;
}

template <int Count> void static_failure() {
  static_remaining = static_initial_count = Count;
  register_static_callbacks(
      LIBC_NAMESPACE::cpp::make_integer_sequence<int, Count>{});
  int previous_blocks = Count / 32 - 1;
  check(block_attempts == previous_blocks &&
        block_allocations == previous_blocks);

  fail_block_allocation = true;
  check(LIBC_NAMESPACE::mmix_atexit_failure_static_atexit(poison) == -1);
  fail_block_allocation = false;
  check(block_attempts == previous_blocks + 1);
  check(block_allocations == previous_blocks && static_remaining == Count);
  check(!static_later_seen);

  check(LIBC_NAMESPACE::mmix_atexit_failure_static_atexit(static_later) == 0);
  check(block_attempts == previous_blocks + 2);
  check(block_allocations == previous_blocks + 1);
  LIBC_NAMESPACE::mmix_atexit_failure_static_cxa_finalize(nullptr);
  check(static_later_seen && static_remaining == 0);
  // The common drain destroys the store and leaves its mutex locked. Do not
  // reset either production object or attempt to use this terminal state again.
  LIBC_NAMESPACE::_Exit(0);
}
} // namespace

TEST(LlvmLibcMMIXAtExitFailureTest, SharedPinFailurePreservesAcceptedCallbacks) {
  EXPECT_EXITS([] { shared_failure(false); }, 0);
}

TEST(LlvmLibcMMIXAtExitFailureTest,
     SharedAllocationFailurePreservesAcceptedCallbacks) {
  EXPECT_EXITS([] { shared_failure(true); }, 0);
}

TEST(LlvmLibcMMIXAtExitFailureTest, StaticFirstBlockGrowthFailure) {
  EXPECT_EXITS([] { static_failure<32>(); }, 0);
}

TEST(LlvmLibcMMIXAtExitFailureTest, StaticLaterBlockGrowthFailure) {
  EXPECT_EXITS([] { static_failure<64>(); }, 0);
}
