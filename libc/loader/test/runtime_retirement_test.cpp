//===-- Deferred retirement coordination tests ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/runtime_retirement.h"
#include "loader/linux/runtime_unwind.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace loader_test;
using LIBC_NAMESPACE::cpp::Atomic;
using Kind = LoaderLocks::Kind;
static thread_local unsigned suppressed;
static thread_local bool pending_cancellation;
static unsigned cancellation_deliveries;
static void restore_cancellation();
static bool fail_mapping;
static unsigned failed_mappings;
static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    std::fprintf(stderr, "retirement check at %d\n", line);
    std::abort();
  }
}
namespace LIBC_NAMESPACE::loader {
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  if (fail_mapping && n == SYS_mmap) {
    ++failed_mappings;
    return -ENOMEM;
  }
  long result = ::syscall(n, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
[[noreturn]] void fatal_startup() { _exit(127); }
} // namespace LIBC_NAMESPACE::loader

static LoaderCancellation hooks{[](uint64_t *saved) -> long {
                                  *saved = suppressed++;
                                  return 0;
                                },
                                [](uint64_t saved) -> long {
                                  suppressed = saved;
                                  if (!saved && pending_cancellation)
                                    restore_cancellation();
                                  return 0;
                                }};

struct Input : DynamicFixture {
  MappedRegion region;
  ImageRegistry::Image record{};
  Input(Address bias, bool startup, RetirementEvents &events)
      : DynamicFixture(startup ? ImageRole::MainPIE : ImageRole::SharedObject) {
    put(0x4900, 8, bias + 0x2008);
    put(0x4908, 8, bias + 0x200c);
    put(0x4910, 8, bias + 0x2010);
    put(56, 2, 9);
    segment(0, PT_PHDR, PF_R, 64, 64, 9 * 56, 9 * 56, 8);
    segment(8, PT_GNU_EH_FRAME, PF_R, 0xc00, 0xc00, 20, 20, 4);
    put(0xc00, 4, 0x011b033b);
    put(0xc04, 4, 0xd00 - 0xc04);
    put(0xc08, 4, 1);
    put(0xc0c, 4, 0x2000 - 0xc00);
    put(0xc10, 4, 0xd00 - 0xc00);
    region = {0, view()};
    check(ElfImage::open_mapped({&region, 1}, 0, 64,
                                startup ? ImageRole::MainPIE
                                        : ImageRole::SharedObject,
                                bias, page, record.elf) == ElfError::None);
    check(DynamicInfo::open(record.elf, record.dynamic) == DynamicError::None);
    record.startup = startup;
    record.discovered = true;
    record.ownership.~ModuleOwnership();
    new (&record.ownership)
        ModuleOwnership(bias, 1, startup, ~uint64_t(0), &events);
  }
};

struct RetirementFixture {
  Atomic<unsigned> wakes{0};
  RetirementEvents events{&wakes, [](void *p) {
                            static_cast<Atomic<unsigned> *>(p)->fetch_add(1);
                          }};
  Input main{0x100000, true, events}, late{0x300000, false, events};
  const ImageRegistry::Image *images[2] = {&main.record, &late.record};
  ImageRegistry::Snapshot initial{1, {images, 1}}, all{2, images};
  RuntimeScopes scopes{page, &events};
  RuntimeQueries queries{page, nullptr, &events};
  RuntimeInitializers initializers;
  RuntimeInitializers::Batch boot{page}, loaded{page};
  LoaderLocks locks;
  InitialStack stack{};
  RuntimeRetirement retirement{page, events, scopes, queries, initializers};
  Arena tickets{page};
  ModuleOwnership::Token tls, tss, process;
  uint64_t first = 0, second = 0;
  unsigned callbacks = 0;
  bool inject_failure = false;
  bool exit_from_callback = false;
  bool cancel_from_callback = false;
  bool fork_from_callback = false;

  RetirementFixture() {
    stack.valid = true;
    LoaderLocks::Session session(locks, hooks);
    LoaderLocks::Guard guard(session, Kind::Mutation);
    check(!initializers.prepare(initial, boot, true));
    check(!initializers.attach(boot, initial));
    check(!initializers.run(boot, session, stack,
                            [](Address, int, char **, char **) {}));
    RuntimeScopes::Transaction start(page), open1(page), open2(page);
    check(!scopes.prepare_startup(initial, start));
    check(!scopes.prepare_unwind(start) && !scopes.publish(start));
    check(!scopes.prepare_open(all, &late.record, false, open1, first));
    check(!scopes.prepare_unwind(open1) && !scopes.publish(open1));
    check(!scopes.prepare_open(all, &late.record, false, open2, second));
    check(!scopes.prepare_unwind(open2) && !scopes.publish(open2));
    check(!initializers.prepare(all, loaded));
    check(!initializers.attach(loaded, all));
    check(!initializers.run(loaded, session, stack,
                            [](Address, int, char **, char **) {}));
    check(!queries.prepare(all));
    queries.publish();
    check(!late.record.ownership.acquire(tickets,
                                         ModuleOwnership::Kind::Runtime, tls));
    check(!late.record.ownership.acquire(tickets,
                                         ModuleOwnership::Kind::Runtime, tss));
    check(!late.record.ownership.acquire(
        tickets, ModuleOwnership::Kind::ProcessCallback, process));
  }
  long drain();
};
static RetirementFixture *fixture;
static void restore_cancellation() {
  check(!suppressed && !fixture->retirement.draining());
  pending_cancellation = false;
  ++cancellation_deliveries;
}
static void finalizer(Address pc) {
  auto &f = *fixture;
  check(suppressed);
  if (f.cancel_from_callback)
    pending_cancellation = true;
  ++f.callbacks;
  if (f.fork_from_callback && f.callbacks == 1) {
    // Detached finalization has no mutation guard. A copied active plan must
    // stay inert in the child rather than waiting for or replaying its owner.
    LoaderLocks::Session inherited(f.locks, hooks);
    check(!f.locks.prepare_fork());
    pid_t child = fork();
    check(child >= 0);
    f.locks.finish_fork(child == 0);
    if (!child) {
      LoaderLocks::Guard old_guard(inherited, Kind::Mutation);
      check(old_guard.error() == -ENOTSUP &&
            !inherited.unlocked_callback_owner());
      LoaderLocks::Session session(f.locks, hooks);
      check(session.error() == -ENOTSUP);
      check(f.retirement.drain(session, finalizer) == -EINVAL);
      check(f.retirement.draining() && f.callbacks == 1);
      _exit(0);
    }
    int status;
    check(waitpid(child, &status, 0) == child && status == 0);
  }
  if (pc >= 0x300000) {
    LoaderLocks::Session nested(f.locks, hooks);
    // Competing safe points schedule, never wait on the current callback.
    check(!f.retirement.drain(nested, finalizer));
    LoaderLocks::Guard guard(nested, Kind::Mutation);
    check(f.retirement.close(f.second, nested) == -EINVAL);
    if (f.callbacks == 1)
      check(!f.late.record.ownership.release(f.process));
    if (f.callbacks == 1 && f.exit_from_callback)
      check(!f.retirement.request_exit(nested));
    if (f.callbacks == 2 && f.inject_failure)
      fail_mapping = true;
  }
}
long RetirementFixture::drain() {
  LoaderLocks::Session session(locks, hooks);
  return retirement.drain(session, finalizer);
}

static void close_handles(RetirementFixture &f) {
  // Two real callers close distinct handles using the same serialized owner.
  struct Call {
    RetirementFixture *f;
    uint64_t handle;
  } calls[] = {{&f, f.first}, {&f, f.second}};
  pthread_t threads[2];
  for (unsigned i = 0; i != 2; ++i)
    check(!pthread_create(
        &threads[i], nullptr,
        [](void *p) -> void * {
          auto &call = *static_cast<Call *>(p);
          LoaderLocks::Session session(call.f->locks, hooks);
          LoaderLocks::Guard guard(session, Kind::Mutation);
          check(!call.f->retirement.close(call.handle, session));
          return nullptr;
        },
        &calls[i]));
  for (auto thread : threads)
    check(!pthread_join(thread, nullptr));
  check(!f.drain() && !f.callbacks);
}

extern "C" int main() {
  RetirementFixture f;
  fixture = &f;
  {
    LoaderLocks::Session session(f.locks, hooks);
    LoaderLocks::Guard guard(session, Kind::Mutation);
    fail_mapping = true;
    check(f.retirement.close(f.first, session) == -ENOMEM);
    fail_mapping = false;
    check(f.scopes.snapshot()->handles.size() == 2);
  }
  close_handles(f);
  UnwindImageView cursor{}, copy{};
  check(!acquire_runtime_unwind_image(&f.scopes, 0x302000, &cursor));
  check(!retain_runtime_unwind_image(&f.scopes, cursor.token, &copy));
  auto phdr = f.queries.acquire();
  {
    LoaderLocks::Session session(f.locks, hooks);
    LoaderLocks::Guard guard(session, Kind::Mutation);
    check(!f.late.record.ownership.release(f.tls));
  }
  check(f.events.requested() && !f.drain() && !f.callbacks);
  {
    LoaderLocks::Session session(f.locks, hooks);
    LoaderLocks::Guard guard(session, Kind::Mutation);
    check(!f.late.record.ownership.release(f.tss));
  }
  f.inject_failure = true;
  f.fork_from_callback = true;
  f.cancel_from_callback = true;
  check(f.drain() == -ENOMEM && f.callbacks == 2);
  check(cancellation_deliveries == 1 && !pending_cancellation && !suppressed);
  f.cancel_from_callback = false;
  fail_mapping = false;
  check(f.events.requested() && !f.drain() && f.callbacks == 2);
  auto *entry = f.retirement.removals();
  check(entry && !entry->next && entry->images.size() == 1);
  check(entry->state == RuntimeRetirement::Removal::WaitingReaders);
  check(f.scopes.snapshot()->registry->images.size() == 1);
  check(f.queries.snapshot()->registry->images.size() == 1);
  check(!drop_runtime_unwind_image(&f.scopes, cursor.token));
  check(!f.drain() &&
        entry->state == RuntimeRetirement::Removal::WaitingReaders);
  check(!drop_runtime_unwind_image(&f.scopes, copy.token));
  phdr = {};
  check(f.events.requested() && !f.drain());
  check(entry->state == RuntimeRetirement::Removal::Ready);
  check(f.late.record.ownership.admission() ==
        ModuleOwnership::State::Retiring);
  // Ready never claims that any mapping was released.
  check(f.callbacks == 2 && f.wakes.load());
  {
    LoaderLocks::Session session(f.locks, hooks);
    LoaderLocks::Guard guard(session, Kind::Mutation);
    check(!f.retirement.request_exit(session));
  }
  check(!f.drain() && f.callbacks == 4);
  check(!f.drain() && f.callbacks == 4);

  RetirementFixture pinned;
  fixture = &pinned;
  auto view = pinned.scopes.acquire();
  view.pin();
  view = {};
  close_handles(pinned);
  {
    LoaderLocks::Session session(pinned.locks, hooks);
    LoaderLocks::Guard guard(session, Kind::Mutation);
    check(!pinned.late.record.ownership.release(pinned.tls));
    check(!pinned.late.record.ownership.release(pinned.tss));
  }
  check(!pinned.drain());
  check(pinned.retirement.removals()->state ==
        RuntimeRetirement::Removal::WaitingReaders);
  check(!pinned.events.requested()); // Intentional pin, not a retry busy loop.

  RetirementFixture exiting;
  fixture = &exiting;
  close_handles(exiting);
  exiting.exit_from_callback = true;
  pthread_t owner;
  check(!pthread_create(
      &owner, nullptr,
      [](void *p) -> void * {
        auto &f = *static_cast<RetirementFixture *>(p);
        {
          LoaderLocks::Session session(f.locks, hooks);
          LoaderLocks::Guard guard(session, Kind::Mutation);
          check(!f.late.record.ownership.release(f.tls));
          check(!f.late.record.ownership.release(f.tss));
        }
        check(f.events.requested());
        // Dispatch the queued work after leaving the release path, without
        // dlopen.
        check(!f.drain());
        return nullptr;
      },
      &exiting));
  check(!pthread_join(owner, nullptr));
  check(exiting.callbacks == 4 && !exiting.events.requested());

  RetirementFixture exhausted;
  fixture = &exhausted;
  close_handles(exhausted);
  {
    LoaderLocks::Session session(exhausted.locks, hooks);
    LoaderLocks::Guard guard(session, Kind::Mutation);
    check(!exhausted.retirement.request_exit(session));
  }
  unsigned before = failed_mappings;
  fail_mapping = true;
  check(!exhausted.drain() && exhausted.callbacks == 4);
  check(!exhausted.drain() && exhausted.callbacks == 4);
  check(failed_mappings == before && !exhausted.retirement.removals());
  check(!exhausted.main.record.ownership.available_for_load());
  check(!exhausted.late.record.ownership.available_for_load());
  fail_mapping = false;

  RetirementFixture held;
  fixture = &held;
  {
    LoaderLocks::Session session(held.locks, hooks);
    {
      LoaderLocks::Guard guard(session, Kind::Mutation);
      check(!held.retirement.request_exit(session));
    }
    const unsigned before = failed_mappings;
    fail_mapping = true;
    check(!held.retirement.drain(session, [](Address pc) {
      auto &f = *fixture;
      // Reverse completion and reverse per-image fini-array order survive
      // exhaustion, including open handles and runtime ownership roots.
      const Address expected[] = {0x302010, 0x302004, 0x102010, 0x102004};
      check(f.callbacks < 4 && pc == expected[f.callbacks++]);
      LoaderLocks::Session nested(f.locks, hooks);
      check(!f.retirement.drain(nested));
    }));
    check(held.callbacks == 4 && failed_mappings == before);
    check(held.scopes.snapshot()->handles.size() == 2);
    check(!held.retirement.removals());
    fail_mapping = false;
  }
  return 0;
}
