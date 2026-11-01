//===-- Native MMIX private signal boundary invariants --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Pure production-helper fault injection. No real signal entry, cursor finish,
// kernel jump, target context authenticity or cancellation admission is proved.
// Disposal flags below are synthetic certificates, NOT unwind-stop evidence.
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <pthread.h>
#include <type_traits>

#include "src/__support/threads/linux/mmix/lifecycle.h"
#include "src/__support/threads/linux/mmix/signal_boundary.h"
#include "src/setjmp/mmix/linux/jump_cleanup_stack.h"

namespace M = LIBC_NAMESPACE::mmix;
using Boundary = M::SignalBoundary;
using Owner = M::SignalBoundaryOwner;
using Point = M::SignalBoundaryPoint;
using Handoff = M::SignalParentHandoff;
using Disposal = M::SignalBoundaryDisposal;
static_assert(!std::is_copy_constructible<Boundary>::value);
static_assert(!std::is_copy_constructible<Owner>::value);
static_assert(!std::is_copy_constructible<Handoff>::value);
static_assert(!std::is_move_constructible<Handoff>::value);
static_assert(std::is_trivially_destructible<Boundary>::value);
static_assert(std::is_trivially_destructible<Handoff>::value);

static bool in_helper;
extern "C" {
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
void *__real_aligned_alloc(size_t, size_t);
int __real_posix_memalign(void **, size_t, size_t);
int __real_pthread_mutex_lock(pthread_mutex_t *);
void *__wrap_malloc(size_t size) {
  assert(!in_helper);
  return __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size) {
  assert(!in_helper);
  return __real_calloc(count, size);
}
void *__wrap_realloc(void *pointer, size_t size) {
  assert(!in_helper);
  return __real_realloc(pointer, size);
}
void __wrap_free(void *pointer) {
  assert(!in_helper);
  __real_free(pointer);
}
void *__wrap_aligned_alloc(size_t alignment, size_t size) {
  assert(!in_helper);
  return __real_aligned_alloc(alignment, size);
}
int __wrap_posix_memalign(void **pointer, size_t alignment, size_t size) {
  assert(!in_helper);
  return __real_posix_memalign(pointer, alignment, size);
}
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex) {
  assert(!in_helper);
  return __real_pthread_mutex_lock(mutex);
}
}

void *operator new(size_t size) {
  assert(!in_helper);
  if (auto *pointer = std::malloc(size))
    return pointer;
  std::abort();
}
void *operator new[](size_t size) { return ::operator new(size); }
void operator delete(void *pointer) noexcept {
  assert(!in_helper);
  std::free(pointer);
}
void operator delete[](void *pointer) noexcept { ::operator delete(pointer); }
void operator delete(void *pointer, size_t) noexcept {
  ::operator delete(pointer);
}
void operator delete[](void *pointer, size_t) noexcept {
  ::operator delete(pointer);
}

template <class T> struct Bytes {
  unsigned char data[sizeof(T)];
  explicit Bytes(const T &object) { std::memcpy(data, &object, sizeof(T)); }
  void unchanged(const T &object) const {
    assert(std::memcmp(data, &object, sizeof(T)) == 0);
  }
};

struct Environment {
  M::ThreadControl thread{}, other_thread{};
  Owner owner{&thread, 41};
  // Opaque cleanup checkpoint: the helper has no access to a cleanup stack.
  uintptr_t cleanup_head = 0x9000;
  unw_context_t context{};

  Environment() {
    thread.unwind_root = {&thread, 0x8000, 0x4000, 0x2000, 41, 7, true};
    thread.saved_signal_mask = 0x1234;
    thread.cancellation_identity = 23;
    thread.cancellation_generation = 19;
    assert(thread.cancellation.request());
    for (size_t i = 0; i != sizeof(context.data) / sizeof(context.data[0]); ++i)
      context.data[i] = 0x10000 + i;
  }
};

struct Wrapper {
  Boundary boundary;
  uint64_t padding[2]{};
};

static Point point_for(Environment &env, Wrapper &wrapper, uint64_t child = 42,
                       uint64_t parent = 41, uint64_t generation = 1) {
  uintptr_t address = reinterpret_cast<uintptr_t>(&wrapper);
  return {&env.thread,
          child,
          parent,
          generation,
          {0x8000, 0x4000, address, address + sizeof(wrapper)}};
}

template <class F>
static void operation(Environment &env, bool expected, F body) {
  Bytes<M::ThreadControl> control(env.thread);
  auto cleanup_head = env.cleanup_head;
  sigset_t before, after;
  assert(sigprocmask(SIG_SETMASK, nullptr, &before) == 0);
  errno = EDOM;
  in_helper = true;
  bool result = body();
  in_helper = false;
  assert(errno == EDOM);
  assert(result == expected);
  control.unchanged(env.thread);
  assert(env.cleanup_head == cleanup_head);
  assert(env.thread.cancellation.ready() &&
         !env.thread.cancellation.terminal());
  assert(sigprocmask(SIG_SETMASK, nullptr, &after) == 0);
  for (int signal = 1; signal < NSIG; ++signal)
    assert(sigismember(&before, signal) == sigismember(&after, signal));
}

template <class F>
static void rejected(Environment &env, Boundary &boundary, Handoff &handoff,
                     F body) {
  Bytes<Owner> owner(env.owner);
  Bytes<Boundary> record(boundary);
  Bytes<Handoff> packet(handoff);
  operation(env, false, body);
  owner.unchanged(env.owner);
  record.unchanged(boundary);
  packet.unchanged(handoff);
}

static bool enter(Environment &env, Boundary &boundary, const Point &point) {
  return env.owner.enter(boundary, point,
                         reinterpret_cast<uintptr_t>(&env.context),
                         &env.cleanup_head, env.context);
}

static Disposal disposed(Environment &env, const Point &point) {
  return {&env.owner,
          point.generation,
          point.child_chain,
          &env.cleanup_head,
          true,
          true,
          true};
}

static void entry_faults() {
  Environment env;
  Wrapper wrapper;
  Handoff packet;
  auto good = point_for(env, wrapper);
  auto &boundary = wrapper.boundary;
  for (unsigned fault = 0; fault != 17; ++fault) {
    auto bad = good;
    switch (fault) {
    case 0:
      bad.thread = nullptr;
      break;
    case 1:
      bad.thread = &env.other_thread;
      break;
    case 2:
      bad.child_chain = 0;
      break;
    case 3:
      bad.child_chain = good.parent_chain;
      break;
    case 4:
      bad.parent_chain = 0;
      break;
    case 5:
      ++bad.parent_chain;
      break;
    case 6:
      bad.generation = 0;
      break;
    case 7:
      bad.generation = 2;
      break;
    case 8:
      bad.wrapper.ro = 0;
      break;
    case 9:
      ++bad.wrapper.ro;
      break;
    case 10:
      bad.wrapper.procedure = 0;
      break;
    case 11:
      ++bad.wrapper.procedure;
      break;
    case 12:
      ++bad.wrapper.sp;
      break;
    case 13:
      bad.wrapper.sp += 8;
      break; // Record begins outside the frame.
    case 14:
      bad.wrapper.limit = bad.wrapper.sp;
      break;
    case 15:
      bad.wrapper.limit = bad.wrapper.sp + sizeof(Boundary) - 8;
      break;
    case 16:
      bad.wrapper.limit = 0;
      break; // Wrapped extent.
    }
    rejected(env, boundary, packet, [&] { return enter(env, boundary, bad); });
  }
  for (uintptr_t locator : {uintptr_t(0), uintptr_t(1)})
    rejected(env, boundary, packet, [&] {
      return env.owner.enter(boundary, good, locator, nullptr, env.context);
    });
  env.owner.base_chain = 0;
  rejected(env, boundary, packet, [&] { return enter(env, boundary, good); });
  env.owner.base_chain = 41;
  env.owner.thread = nullptr;
  rejected(env, boundary, packet, [&] { return enter(env, boundary, good); });
  env.owner.thread = &env.thread;

  operation(env, true, [&] { return enter(env, boundary, good); });
  rejected(env, boundary, packet, [&] { return enter(env, boundary, good); });
  Owner other_owner(&env.thread, 41);
  Bytes<Owner> other_before(other_owner);
  rejected(env, boundary, packet, [&] {
    return other_owner.enter(boundary, good,
                             reinterpret_cast<uintptr_t>(&env.context), nullptr,
                             env.context);
  });
  other_before.unchanged(other_owner);

  for (unsigned fault = 0; fault != 7; ++fault) {
    auto bad = good;
    switch (fault) {
    case 0:
      bad.thread = &env.other_thread;
      break;
    case 1:
      ++bad.child_chain;
      break;
    case 2:
      ++bad.parent_chain;
      break;
    case 3:
      ++bad.generation;
      break;
    case 4:
      bad.wrapper.ro += 8;
      break;
    case 5:
      bad.wrapper.procedure += 4;
      break;
    case 6:
      bad.wrapper.limit += 8;
      break;
    }
    rejected(env, boundary, packet,
             [&] { return env.owner.normal_return(boundary, bad); });
  }
  boundary.owner = &other_owner;
  rejected(env, boundary, packet,
           [&] { return env.owner.normal_return(boundary, good); });
  boundary.owner = &env.owner;
  ++boundary.point.generation;
  rejected(env, boundary, packet,
           [&] { return env.owner.normal_return(boundary, good); });
  --boundary.point.generation;
  ++env.owner.live_generation;
  rejected(env, boundary, packet,
           [&] { return env.owner.normal_return(boundary, good); });
  --env.owner.live_generation;
  operation(env, true, [&] { return env.owner.normal_return(boundary, good); });
  assert(!boundary.owner && !env.owner.live && !env.owner.live_generation);
  rejected(env, boundary, packet,
           [&] { return env.owner.normal_return(boundary, good); });
  rejected(env, boundary, packet, [&] { return enter(env, boundary, good); });
  auto next = good;
  ++next.generation;
  operation(env, true, [&] { return enter(env, boundary, next); });
  rejected(env, boundary, packet,
           [&] { return env.owner.normal_return(boundary, good); });
  operation(env, true, [&] { return env.owner.normal_return(boundary, next); });
}

static void nested_lifo() {
  Environment env;
  // Test depth is not a production capacity: the helper has no fixed limit.
  constexpr unsigned depth = 96;
  Wrapper wrappers[depth];
  Point points[depth];
  Handoff packet;
  for (unsigned i = 0; i != depth; ++i) {
    points[i] = point_for(env, wrappers[i], 42 + i, 41 + i, i + 1);
    // Unrelated chains may have decreasing or identical rO values.
    points[i].wrapper.ro = i % 2 ? 8 : UINT64_MAX - 7;
    operation(env, true,
              [&] { return enter(env, wrappers[i].boundary, points[i]); });
    assert(env.owner.live == &wrappers[i].boundary);
  }
  rejected(env, wrappers[0].boundary, packet, [&] {
    return env.owner.normal_return(wrappers[0].boundary, points[0]);
  });
  Wrapper extra;
  auto bad = point_for(env, extra, 42, 41 + depth, depth + 1);
  rejected(env, extra.boundary, packet,
           [&] { return enter(env, extra.boundary, bad); });
  auto &top = wrappers[depth - 1].boundary;
  ++top.previous_generation;
  rejected(env, top, packet,
           [&] { return env.owner.normal_return(top, points[depth - 1]); });
  --top.previous_generation;
  for (unsigned i = depth; i != 0; --i) {
    operation(env, true, [&] {
      return env.owner.normal_return(wrappers[i - 1].boundary, points[i - 1]);
    });
    assert(env.owner.live_generation == i - 1);
    assert(env.owner.live == (i == 1 ? nullptr : &wrappers[i - 2].boundary));
    assert(env.owner.generation == depth);
  }
}

static void handoff_disposal_and_retirement() {
  Environment env;
  Wrapper outer, inner, extra;
  Handoff packet, parent_packet;
  auto outer_point = point_for(env, outer);
  auto inner_point = point_for(env, inner, 43, 42, 2);
  inner_point.wrapper.ro = 8;
  operation(env, true, [&] { return enter(env, outer.boundary, outer_point); });
  env.context.data[0] = 0x223344;
  Bytes<unw_context_t> expected_inner(env.context);
  operation(env, true, [&] { return enter(env, inner.boundary, inner_point); });
  // Parent context was captured by value, not borrowed from mutable input.
  std::memset(&env.context, 0xCC, sizeof(env.context));
  auto certificate = disposed(env, inner_point);
  auto &boundary = inner.boundary;
  rejected(env, boundary, packet,
           [&] { return env.owner.detach(boundary, inner_point, packet); });
  rejected(env, boundary, packet,
           [&] { return env.owner.consume(packet, &env.thread, 42, 2); });
  for (unsigned fault = 0; fault != 7; ++fault) {
    auto bad = certificate;
    switch (fault) {
    case 0:
      bad.completed_segment = false;
      break;
    case 1:
      bad.cursors_disposed = false;
      break;
    case 2:
      bad.observers_disposed = false;
      break;
    case 3:
      bad.owner = nullptr;
      break;
    case 4:
      --bad.generation;
      break;
    case 5:
      --bad.child_chain;
      break;
    case 6:
      bad.cleanup_boundary = nullptr;
      break;
    }
    rejected(env, boundary, packet, [&] {
      return env.owner.prepare_handoff(boundary, inner_point, bad, packet);
    });
  }
  rejected(env, outer.boundary, packet, [&] {
    return env.owner.prepare_handoff(outer.boundary, outer_point,
                                     disposed(env, outer_point), packet);
  });
  operation(env, true, [&] {
    return env.owner.prepare_handoff(boundary, inner_point, certificate,
                                     packet);
  });
  expected_inner.unchanged(packet.saved_parent);
  assert(env.owner.live == &boundary && env.owner.handoff_generation == 2);
  rejected(env, boundary, packet, [&] {
    return env.owner.prepare_handoff(boundary, inner_point, certificate,
                                     packet);
  });
  rejected(env, boundary, parent_packet, [&] {
    return env.owner.prepare_handoff(boundary, inner_point, certificate,
                                     parent_packet);
  });
  rejected(env, boundary, packet,
           [&] { return env.owner.normal_return(boundary, inner_point); });
  rejected(env, extra.boundary, packet, [&] {
    return enter(env, extra.boundary, point_for(env, extra, 44, 43, 3));
  });
  rejected(env, boundary, packet,
           [&] { return env.owner.consume(packet, &env.thread, 42, 2); });
  auto stale = inner_point;
  --stale.generation;
  rejected(env, boundary, packet,
           [&] { return env.owner.detach(boundary, stale, packet); });
  ++packet.parent_chain;
  rejected(env, boundary, packet,
           [&] { return env.owner.detach(boundary, inner_point, packet); });
  --packet.parent_chain;
  auto wrong_child = inner_point;
  ++wrong_child.child_chain;
  rejected(env, boundary, packet,
           [&] { return env.owner.detach(boundary, wrong_child, packet); });
  auto wrong_parent = inner_point;
  --wrong_parent.parent_chain;
  rejected(env, boundary, packet,
           [&] { return env.owner.detach(boundary, wrong_parent, packet); });
  operation(env, true,
            [&] { return env.owner.detach(boundary, inner_point, packet); });
  assert(env.owner.live == &outer.boundary && env.owner.live_generation == 1);
  assert(!boundary.owner && !boundary.previous &&
         !boundary.previous_generation);
  rejected(env, boundary, packet,
           [&] { return env.owner.detach(boundary, inner_point, packet); });
  // All subsequent continuation operations must be independent of this storage.
  std::memset(static_cast<void *>(&inner), 0xA5, sizeof(inner));
  expected_inner.unchanged(packet.saved_parent);
  for (uint64_t chain : {uint64_t(0), uint64_t(41), uint64_t(43)})
    rejected(env, outer.boundary, packet,
             [&] { return env.owner.consume(packet, &env.thread, chain, 2); });
  rejected(env, outer.boundary, packet,
           [&] { return env.owner.consume(packet, &env.other_thread, 42, 2); });
  rejected(env, outer.boundary, packet,
           [&] { return env.owner.consume(packet, &env.thread, 42, 1); });
  Owner wrong_owner(&env.thread, 42);
  wrong_owner.generation = wrong_owner.handoff_generation = 2;
  Bytes<Owner> wrong_before(wrong_owner);
  rejected(env, outer.boundary, packet,
           [&] { return wrong_owner.consume(packet, &env.thread, 42, 2); });
  wrong_before.unchanged(wrong_owner);
  rejected(env, outer.boundary, packet, [&] {
    return env.owner.normal_return(outer.boundary, outer_point);
  });
  operation(env, true,
            [&] { return env.owner.consume(packet, &env.thread, 42, 2); });
  expected_inner.unchanged(packet.saved_parent);
  assert(packet.phase == Handoff::Phase::Consumed);
  rejected(env, outer.boundary, packet,
           [&] { return env.owner.consume(packet, &env.thread, 42, 2); });
  rejected(env, outer.boundary, packet, [&] {
    return env.owner.prepare_handoff(outer.boundary, outer_point,
                                     disposed(env, outer_point), packet);
  });

  // Restore and retire the older live generation, NOT the high-water mark.
  Bytes<unw_context_t> expected_outer(outer.boundary.saved_parent);
  operation(env, true, [&] {
    return env.owner.prepare_handoff(outer.boundary, outer_point,
                                     disposed(env, outer_point), parent_packet);
  });
  operation(env, true, [&] {
    return env.owner.detach(outer.boundary, outer_point, parent_packet);
  });
  std::memset(static_cast<void *>(&outer), 0x5A, sizeof(outer));
  assert(!env.owner.live && !env.owner.live_generation);
  operation(env, true, [&] {
    return env.owner.consume(parent_packet, &env.thread, 41, 1);
  });
  expected_outer.unchanged(parent_packet.saved_parent);
  assert(!env.owner.handoff_generation && env.owner.generation == 2);
}

static void handoff_must_not_be_in_wrapper() {
  Environment env;
  struct ChildStorage {
    Wrapper wrapper;
    Handoff handoff;
  } child;
  auto point = point_for(env, child.wrapper);
  point.wrapper.limit = reinterpret_cast<uintptr_t>(&child) + sizeof(child);
  operation(env, true,
            [&] { return enter(env, child.wrapper.boundary, point); });
  rejected(env, child.wrapper.boundary, child.handoff, [&] {
    return env.owner.prepare_handoff(child.wrapper.boundary, point,
                                     disposed(env, point), child.handoff);
  });
  operation(env, true, [&] {
    return env.owner.normal_return(child.wrapper.boundary, point);
  });
}

static void generation_exhaustion() {
  Environment env;
  Wrapper wrapper;
  Handoff packet;
  // Fault injection at the end of the counter, not a production reset API.
  env.owner.generation = UINT64_MAX - 1;
  auto last = point_for(env, wrapper, 42, 41, UINT64_MAX);
  operation(env, true, [&] { return enter(env, wrapper.boundary, last); });
  operation(env, true,
            [&] { return env.owner.normal_return(wrapper.boundary, last); });
  for (uint64_t generation : {uint64_t(0), uint64_t(1), UINT64_MAX}) {
    auto bad = last;
    bad.generation = generation;
    rejected(env, wrapper.boundary, packet,
             [&] { return enter(env, wrapper.boundary, bad); });
    assert(env.owner.generation == UINT64_MAX);
  }
}

static bool retire_for_jump(void *opaque) {
  auto &boundary = *static_cast<Boundary *>(opaque);
  return boundary.owner &&
         boundary.owner->normal_return(boundary, boundary.point);
}

static void jump_checkpoints() {
  Environment env;
  Wrapper outer, inner;
  M::JumpCleanupStack::Node *head = nullptr;
  M::JumpCleanupStack::Node first, second, unknown;
  auto outer_point = point_for(env, outer);
  auto inner_point = point_for(env, inner, 43, 42, 2);
  operation(env, true, [&] {
    return enter(env, outer.boundary, outer_point) &&
           M::JumpCleanupStack::push(head, first, retire_for_jump,
                                    &outer.boundary);
  });
  operation(env, true, [&] {
    return enter(env, inner.boundary, inner_point) &&
           M::JumpCleanupStack::push(head, second, retire_for_jump,
                                    &inner.boundary);
  });
  // An inner target keeps both wrappers; invalid targets have no side effects.
  operation(env, true,
            [&] { return M::JumpCleanupStack::unwind(head, &second); });
  operation(env, false,
            [&] { return M::JumpCleanupStack::unwind(head, &unknown); });
  assert(env.owner.live == &inner.boundary && first.linked() && second.linked());
  operation(env, true,
            [&] { return M::JumpCleanupStack::unwind(head, &first); });
  assert(!second.linked() && !inner.boundary.owner);
  assert(env.owner.live == &outer.boundary && env.owner.generation == 2);
  operation(env, true,
            [&] { return M::JumpCleanupStack::unwind(head, nullptr); });
  assert(!first.linked() && !outer.boundary.owner && !env.owner.live && !head);
  assert(env.owner.generation == 2);
}

int main() {
  sigset_t blocked, original;
  assert(sigfillset(&blocked) == 0);
  assert(sigprocmask(SIG_SETMASK, &blocked, &original) == 0);
  entry_faults();
  nested_lifo();
  handoff_disposal_and_retirement();
  handoff_must_not_be_in_wrapper();
  generation_exhaustion();
  jump_checkpoints();
  assert(sigprocmask(SIG_SETMASK, &original, nullptr) == 0);
}
