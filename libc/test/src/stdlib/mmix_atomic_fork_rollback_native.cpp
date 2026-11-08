// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Reuse production lifecycle/registry/ForkOperation and the existing injected
// non-atomic service/kernel boundaries. A separate C TU supplies the real atomic
// provider and test-only access to its private lock table.
#define main retained_exit_fork_main
#include "mmix_normal_exit_fork_native.cpp"
#undef main

extern "C" size_t test_atomic_lock_count();
extern "C" void test_atomic_lock_set(size_t, uintptr_t);
extern "C" uintptr_t test_atomic_lock_get(size_t);

int main() {
  selected = Case::AtomicRollback;
  M::ThreadControl caller;
  caller.attributes.tid = ::getpid();
  M::current_control = &caller;
  assert(I::activate_main_thread(caller));
  caller.unwind_root = {&caller, 8, 16, 24, 4, 5, true,
                        M::UnwindRootKind::Thread};
  const auto saved_mask = mask;
  const auto count = test_atomic_lock_count();
  assert(count >= 3);
  auto released = [&] {
    assert(mask == saved_mask && !caller.cancellation_suppression.load());
    assert(!M::ForkOperation::active_on_this_thread());
    assert(!notification_snapshot && !aio_snapshot && !aio_frozen &&
           !loader_snapshot && !allocator_snapshot && !credential_snapshot);
    assert(!clone_attempts && !caller.fork_recovery.is_child());
    assert(LIBC_NAMESPACE::File::list_lock.try_lock() ==
           LIBC_NAMESPACE::MutexError::NONE);
    LIBC_NAMESPACE::File::unlock_list();
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.live_threads() == 1 && !lock.pending_creations());
    assert(lock.owner(caller) == M::ThreadOwner::Joinable);
  };
  auto table = [&](uintptr_t expected) {
    for (size_t i = 0; i != count; ++i)
      assert(test_atomic_lock_get(i) == expected);
  };
  auto retry = [&](bool minimal) {
    {
      M::ForkOperation operation(minimal);
      assert(!operation.error() && !operation.prepare_runtime());
      table(1);
      assert(allocator_snapshot && !operation.freeze_registry());
      operation.restore_runtime(false);
      table(0);
      // A fresh owner after explicit restore must survive destruction.
      test_atomic_lock_set(0, 7);
    }
    released();
    assert(test_atomic_lock_get(0) == 7);
    test_atomic_lock_set(0, 0);
    table(0);
  };
  for (bool minimal : {false, true}) {
    for (size_t busy : {size_t(0), count / 2, count - 1}) {
      auto original = [&](size_t i) -> uintptr_t {
        return i == busy ? 7 : i > busy && (i & 1) ? i + 2 : 0;
      };
      for (size_t i = 0; i != count; ++i)
        test_atomic_lock_set(i, original(i));
      for (unsigned attempt = 0; attempt != 2; ++attempt) {
        {
          M::ForkOperation operation(minimal);
          assert(!operation.error() && operation.prepare_runtime() == EAGAIN);
          assert(!allocator_snapshot && !clone_attempts);
        }
        released();
        for (size_t i = 0; i != count; ++i)
          assert(test_atomic_lock_get(i) == original(i));
      }
      for (size_t i = 0; i != count; ++i)
        test_atomic_lock_set(i, 0);
      retry(minimal);
    }
    // Atomic preparation succeeds before the next service rejects. Its locks
    // now belong to ForkOperation and must be released by outer rollback.
    snapshot_failure = SnapshotFailure::Allocator;
    {
      M::ForkOperation operation(minimal);
      assert(!operation.error() && operation.prepare_runtime() == EAGAIN);
      assert(!allocator_snapshot);
      table(1);
    }
    snapshot_failure = SnapshotFailure::None;
    released();
    table(0);
    retry(minimal);
  }
}
