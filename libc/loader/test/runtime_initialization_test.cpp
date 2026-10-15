//===-- Resident late initializer tests -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/runtime_initialization.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace loader_test;
using LIBC_NAMESPACE::cpp::Atomic;
using Kind = LoaderLocks::Kind;
static thread_local unsigned disabled;
static thread_local unsigned callbacks;
static Atomic<unsigned> waits{0};
static bool fail_mapping;
static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    std::fprintf(stderr, "runtime initialization check at %d\n", line);
    std::abort();
  }
}
namespace LIBC_NAMESPACE::loader {
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  check(disabled != 0);
  if (fail_mapping && n == SYS_mmap)
    return -ENOMEM;
  if (n == SYS_futex && b == FUTEX_WAIT_PRIVATE)
    waits.fetch_add(1);
  long result = ::syscall(n, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
} // namespace LIBC_NAMESPACE::loader

static long enter(uint64_t *saved) {
  *saved = disabled++;
  return 0;
}
static long leave(uint64_t saved) {
  check(disabled == saved + 1);
  disabled = static_cast<unsigned>(saved);
  return 0;
}
static void begin_callback() {
  check(disabled);
  ++callbacks;
}
static void end_callback() {
  check(disabled && callbacks);
  --callbacks;
}
static LoaderCancellation hooks{enter, leave, begin_callback, end_callback};
static LoaderLocks locks;
static InitialStack *initial_stack;
static Address trace[64];
static unsigned trace_count;
static void record(Address pc) {
  check(disabled && callbacks && trace_count < 64);
  trace[trace_count++] = pc;
}

struct Input : DynamicFixture {
  MappedRegion region;
  ImageRegistry::Image record{};
  explicit Input(Address bias, bool startup = false)
      : DynamicFixture(startup ? ImageRole::MainPIE : ImageRole::SharedObject) {
    put(0x4900, 8, bias + 0x2008);
    put(0x4908, 8, bias + 0x200c);
    put(0x4910, 8, bias + 0x2010);
    region = {0, view()};
    check(ElfImage::open_mapped({&region, 1}, 0, 64,
                                startup ? ImageRole::MainPIE
                                        : ImageRole::SharedObject,
                                bias, page, record.elf) == ElfError::None);
    record.startup = startup;
    record.discovered = true;
  }
};

static RuntimeInitializers *manager;
static RuntimeInitializers::Batch *outer_batch;
static RuntimeInitializers::Batch *nested_batch;
static const ImageRegistry::Snapshot *nested_registry;
static bool nesting, exiting, block_worker;
static pthread_t worker;
static Atomic<unsigned> worker_done{0};
static void finalizer(Address pc) {
  record(pc);
  LoaderLocks::Session session(locks, hooks);
  LoaderLocks::Guard guard(session, Kind::Mutation);
  check(!guard.error() && !manager->finalize(session, finalizer));
}
static void initializer(Address pc, int argc, char **argv, char **envp);
static void *reopen(void *) {
  LoaderLocks::Session session(locks, hooks);
  LoaderLocks::Guard guard(session, Kind::Mutation);
  check(!guard.error());
  check(!manager->run(*outer_batch, session, *initial_stack, initializer));
  worker_done.store(1);
  return nullptr;
}
static void initializer(Address pc, int argc, char **argv, char **envp) {
  record(pc);
  check(argc == 1 && reinterpret_cast<uintptr_t>(argv) == 0x100 &&
        reinterpret_cast<uintptr_t>(envp) == 0x200);
  // A callback may acquire the short lock: the scheduler must not hold it.
  LoaderLocks::Session session(locks, hooks);
  LoaderLocks::Guard mutation(session, Kind::Mutation);
  check(!mutation.error());
  {
    LoaderLocks::Guard registry(session, Kind::Registry);
    check(!registry.error());
    check(manager->run(*outer_batch, session, *initial_stack, initializer) ==
          -EINVAL);
  }
  if (pc == 0x302000 && nesting) {
    nesting = false;
    // Same-thread reopen of this cycle must not replay pending siblings.
    check(!manager->run(*outer_batch, session, *initial_stack, initializer));
    auto &nested = *nested_batch;
    check(!manager->prepare(*nested_registry, nested));
    check(!manager->attach(nested, *nested_registry));
    check(!manager->run(nested, session, *initial_stack, initializer));
  }
  if (pc == 0x302000 && block_worker) {
    block_worker = false;
    check(!pthread_create(&worker, nullptr, reopen, nullptr));
    while (!waits.load())
      sched_yield();
    check(!worker_done.load());
  }
  if (pc == 0x302008 && exiting)
    check(!manager->finalize(session, finalizer));
}

static void seed(RuntimeInitializers &m, RuntimeInitializers::Batch &batch,
                 const ImageRegistry::Snapshot &startup) {
  check(!m.prepare(startup, batch) && !m.attach(batch, startup));
  check(!m.adopt_startup(batch, startup.images));
}
static void expect(const Address *values, size_t n) {
  check(trace_count == n);
  for (size_t i = 0; i < n; ++i)
    check(trace[i] == values[i]);
}

extern "C" int main() {
  InitialStack stack;
  initial_stack = &stack;
  stack.valid = true;
  stack.argc = 1;
  stack.argv = 0x100;
  stack.envp = 0x200;
  Input main(0x100000, true), a(0x300000), b(0x500000), nested(0x700000);
  const ImageRegistry::Image *a_deps[] = {&b.record};
  const ImageRegistry::Image *b_deps[] = {&a.record};
  a.record.dependencies = a_deps;
  b.record.dependencies = b_deps;
  const ImageRegistry::Image *images[] = {&main.record, &a.record, &b.record,
                                          &nested.record};
  ImageRegistry::Snapshot startup{1, {images, 1}};
  ImageRegistry::Snapshot late{2, {images, 3}};
  ImageRegistry::Snapshot extra{3, images};
  RuntimeInitializers m;
  RuntimeInitializers::Batch start(page), outer(page), stale(page), inner(page);
  manager = &m;
  outer_batch = &outer;
  nested_batch = &inner;
  nested_registry = &extra;
  {
    LoaderLocks::Session session(locks, hooks);
    check(m.run(outer, session, stack, initializer) == -EINVAL);
    LoaderLocks::Guard guard(session, Kind::Mutation);
    check(!guard.error());
    seed(m, start, startup);
    check(!m.prepare(late, stale));
    check(!m.prepare(late, outer));
    check(m.attach(outer, extra) == -EINVAL);
    fail_mapping = true;
    check(!m.attach(outer, late));
    fail_mapping = false;
    check(m.attach(stale, late) == -EAGAIN && !stale.abort());
    nesting = block_worker = true;
    check(!m.run(outer, session, stack, initializer));
    check(!worker_done.load());
  }
  check(!pthread_join(worker, nullptr) && worker_done.load() && !disabled);
  const Address init[] = {0x302000, 0x702000, 0x702008, 0x70200c, 0x302008,
                          0x30200c, 0x502000, 0x502008, 0x50200c};
  expect(init, sizeof(init) / sizeof(*init));
  trace_count = 0;
  {
    LoaderLocks::Session session(locks, hooks);
    LoaderLocks::Guard guard(session, Kind::Mutation);
    check(!m.finalize(session, finalizer));
    check(!m.finalize(session, finalizer));
    check(m.run(outer, session, stack, initializer) == -ECANCELED);
  }
  const Address fini[] = {0x502010, 0x502004, 0x302010, 0x302004,
                          0x702010, 0x702004, 0x102010, 0x102004};
  expect(fini, sizeof(fini) / sizeof(*fini));
  check(outer.abort() == -EBUSY);

  // Exit from the second callback must not finalize its unfinished object
  // or any as-yet-unrun cycle member.
  RuntimeInitializers exit_manager;
  RuntimeInitializers::Batch exit_start(page), exit_late(page),
      exit_nested(page);
  manager = &exit_manager;
  outer_batch = &exit_late;
  nested_batch = &exit_nested;
  trace_count = 0;
  exiting = nesting = true;
  {
    LoaderLocks::Session session(locks, hooks);
    LoaderLocks::Guard guard(session, Kind::Mutation);
    seed(exit_manager, exit_start, startup);
    check(!exit_manager.prepare(late, exit_late));
    check(!exit_manager.attach(exit_late, late));
    check(exit_manager.run(exit_late, session, stack, initializer) ==
          -ECANCELED);
  }
  const Address stopped[] = {0x302000, 0x702000, 0x702008, 0x70200c, 0x302008,
                             0x702010, 0x702004, 0x102010, 0x102004};
  expect(stopped, sizeof(stopped) / sizeof(*stopped));

  // An acyclic dependency runs before its consumer; copying callbacks during
  // preparation makes a later array mutation irrelevant to the chosen plan.
  b.record.dependencies = {};
  RuntimeInitializers acyclic;
  RuntimeInitializers::Batch acyclic_start(page), acyclic_late(page);
  manager = &acyclic;
  outer_batch = &acyclic_late;
  trace_count = 0;
  exiting = false;
  {
    LoaderLocks::Session session(locks, hooks);
    LoaderLocks::Guard guard(session, Kind::Mutation);
    seed(acyclic, acyclic_start, startup);
    check(!acyclic.prepare(late, acyclic_late));
    check(!acyclic.attach(acyclic_late, late));
    a.put(0x4900, 8, 0);
    check(!acyclic.run(acyclic_late, session, stack, initializer));
    check(!acyclic.run(acyclic_late, session, stack, initializer));
  }
  const Address dependencies[] = {0x502000, 0x502008, 0x50200c,
                                  0x302000, 0x302008, 0x30200c};
  expect(dependencies, sizeof(dependencies) / sizeof(*dependencies));

  // Bad callback snapshots and allocation failures must stay unpublished.
  RuntimeInitializers failed;
  RuntimeInitializers::Batch bad(page), oom(page);
  {
    LoaderLocks::Session session(locks, hooks);
    LoaderLocks::Guard guard(session, Kind::Mutation);
    a.put(0x4900, 8, 0);
    check(failed.prepare(late, bad) == -ENOEXEC && !bad.abort());
    check(failed.attach(bad, late) == -EINVAL);
    fail_mapping = true;
    check(failed.prepare(late, oom) == -ENOMEM);
    fail_mapping = false;
    check(!oom.abort());
  }
  std::puts(
      "runtime initializer nesting, cycles, concurrent reopen, finalizers "
      "and failure checks passed");
  return 0;
}
