//===-- Native shared condition retirement regression ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "src/__support/threads/CndVar.h"

namespace L = LIBC_NAMESPACE;
struct Cancel {};
using Deadline = L::cpp::optional<L::CndVar::Timeout>;

static void poison(L::CndVar &cond) {
  auto *bytes = reinterpret_cast<unsigned char *>(&cond);
  for (unsigned i = 0; i < sizeof(cond); ++i)
    bytes[i] = 1;
}
static void check_poison(L::CndVar &cond) {
  auto *bytes = reinterpret_cast<unsigned char *>(&cond);
  for (unsigned i = 0; i < sizeof(cond); ++i)
    assert(bytes[i] == 1);
}
struct RetiringMutex {
  L::CndVar &cond;
  L::Futex futex{0};
  unsigned locks = 0;
  void unlock() {}
  L::MutexError lock() {
    ++locks;
    cond.reset();
    poison(cond);
    return L::MutexError::NONE;
  }
  L::Futex &get_raw_futex() { return futex; }
  bool can_be_requeued() { return false; }
};

// Inject a wait result and retire storage at mutex reacquisition. Both
// exception locations must leave storage unchanged, as must success and
// timeout.
static void retirement_paths() {
  for (bool shared : {false, true}) {
    for (unsigned mode = 0; mode != 5; ++mode) {
      L::CndVar cond{shared};
      RetiringMutex mutex{cond};
      bool caught = false;
      try {
        auto result = cond.wait_with(
            &mutex, L::cpp::nullopt,
            [&](L::RawMutex &, Deadline) {
              if (mode == 1)
                return false;
              if (mode == 4)
                throw Cancel{};
              cond.notify_one();
              if (mode == 2)
                throw Cancel{};
              return true;
            },
            [=] {
              if (mode == 3)
                throw Cancel{};
            },
            [=](L::Futex &, L::FutexWordType, Deadline) -> L::ErrorOr<int> {
              if (mode == 2 || mode == 4)
                throw Cancel{};
              if (mode == 1)
                return L::cpp::unexpected(ETIMEDOUT);
              return 0;
            });
        assert(result == (mode == 1 ? L::CndVarResult::Timeout
                                    : L::CndVarResult::Success));
      } catch (Cancel &) {
        caught = true;
      }
      assert(caught == (mode >= 2));
      assert(mutex.locks == 1);
      check_poison(cond);
    }
  }
}

struct SharedState {
  L::CndVar cond{true};
  L::Mutex mutex{false, false, false, false};
  L::Futex registered{0}, release{0};
  long destroyer;
  bool synthetic_high = false;
};
static void *waiter(void *opaque) {
  auto &s = *static_cast<SharedState *>(opaque);
  assert(s.mutex.lock() == L::MutexError::NONE);
  auto result = s.cond.wait_with(
      &s.mutex, L::cpp::nullopt, [](L::RawMutex &, Deadline) { return true; },
      [] {},
      [&](L::Futex &, L::FutexWordType, Deadline) -> L::ErrorOr<int> {
        s.registered.store(1);
        s.registered.notify_all();
        while (s.release.load() == 0)
          s.release.wait(0);
        return 0;
      });
  assert(result == L::CndVarResult::Success);
  check_poison(s.cond);
  s.mutex.unlock();
  return nullptr;
}
static void *release_waiter(void *opaque) {
  auto &s = *static_cast<SharedState *>(opaque);
  char path[128];
  snprintf(path, sizeof(path), "/proc/self/task/%ld/syscall", s.destroyer);
  // Observe the actual blocking syscall on this condition's count before
  // allowing its last reference to drain. A mere scheduling delay is no proof.
  for (;;) {
    FILE *f = fopen(path, "r");
    assert(f);
    long call;
    unsigned long address, op, expected;
    int n = fscanf(f, "%ld %lx %lx %lx", &call, &address, &op, &expected);
    fclose(f);
    if (n == 4 && call == SYS_futex &&
        address == reinterpret_cast<unsigned long>(&s.cond) +
                       (sizeof(size_t) == 8 &&
                                (s.synthetic_high == L::Endian::IS_LITTLE)
                            ? 4
                            : 0) &&
        op == FUTEX_WAIT && expected == 1)
      break;
    sched_yield();
  }
  if (s.synthetic_high) {
    // CndVar's first member is the shared count. This diagnostic seeds 2^32
    // references without billions of threads, then models their final release.
    static_assert(__is_standard_layout(L::CndVar));
    auto *count = reinterpret_cast<size_t *>(&s.cond);
    __atomic_store_n(count, size_t{0}, __ATOMIC_RELEASE);
    auto *bytes = reinterpret_cast<unsigned char *>(count);
    L::Futex::notify_all_at(bytes, true);
    L::Futex::notify_all_at(bytes + 4, true);
  } else {
    s.release.store(1);
    s.release.notify_all();
  }
  return nullptr;
}
static void blocking_retirement() {
  SharedState state;
  state.destroyer = syscall(SYS_gettid);
  pthread_t worker, releaser;
  assert(pthread_create(&worker, nullptr, waiter, &state) == 0);
  while (state.registered.load() == 0)
    state.registered.wait(0);
  assert(state.mutex.lock() == L::MutexError::NONE);
  assert(pthread_create(&releaser, nullptr, release_waiter, &state) == 0);
  state.cond.reset();
  poison(state.cond);
  state.mutex.unlock();
  assert(pthread_join(worker, nullptr) == 0);
  assert(pthread_join(releaser, nullptr) == 0);
  check_poison(state.cond);
}
static void high_word_retirement() {
  if constexpr (sizeof(size_t) == 8) {
    SharedState state;
    state.destroyer = syscall(SYS_gettid);
    state.synthetic_high = true;
    size_t count = size_t{1} << 32;
    memcpy(&state.cond, &count, sizeof(count));
    pthread_t releaser;
    assert(pthread_create(&releaser, nullptr, release_waiter, &state) == 0);
    state.cond.reset();
    assert(pthread_join(releaser, nullptr) == 0);
  }
}
// Exercise ordinary predicate loops through the actual generic implementation
// for both private queue propagation and shared futex notification.
struct Queue {
  L::CndVar available, consumed;
  L::Mutex mutex{false, false, false, false};
  unsigned pending = 0, total = 0;
  bool done = false;
  Queue(bool shared) : available(shared), consumed(shared) {}
};
static void *consumer(void *opaque) {
  auto &q = *static_cast<Queue *>(opaque);
  assert(q.mutex.lock() == L::MutexError::NONE);
  for (;;) {
    while (!q.pending && !q.done)
      assert(q.available.wait(&q.mutex) == L::CndVarResult::Success);
    if (q.done)
      break;
    assert(q.pending == 1);
    --q.pending;
    ++q.total;
    q.consumed.notify_one();
  }
  q.mutex.unlock();
  return nullptr;
}
static void predicate_loops(bool shared) {
  Queue q(shared);
  pthread_t threads[4];
  for (auto &thread : threads)
    assert(pthread_create(&thread, nullptr, consumer, &q) == 0);
  assert(q.mutex.lock() == L::MutexError::NONE);
  for (unsigned i = 0; i != 100; ++i) {
    q.pending = 1;
    if (i % 2)
      q.available.notify_one();
    else
      q.available.broadcast();
    while (q.pending)
      assert(q.consumed.wait(&q.mutex) == L::CndVarResult::Success);
    assert(q.total == i + 1);
  }
  q.done = true;
  q.available.broadcast();
  q.mutex.unlock();
  for (auto thread : threads)
    assert(pthread_join(thread, nullptr) == 0);
  q.available.reset();
  q.consumed.reset();
}
int main() {
  alarm(15);
  retirement_paths();
  blocking_retirement();
  high_word_retirement();
  predicate_loops(false);
  predicate_loops(true);
  alarm(0);
}
