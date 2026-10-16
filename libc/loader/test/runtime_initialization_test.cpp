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
    if (startup) {
      --entries; // Replace the fixture's terminating DT_NULL.
      add(DT_PREINIT_ARRAY, 0x4920);
      add(DT_PREINIT_ARRAYSZ, 8);
      put(0x4000 + entries * 16, 8, DT_NULL);
      put(0x4008 + entries * 16, 8, 0);
      put(ph(5, 32), 8, (entries + 1) * 16);
      put(ph(5, 40), 8, (entries + 1) * 16);
      put(0x4920, 8, bias + 0x2014);
    }
    region = {0, view()};
    check(ElfImage::open_mapped({&region, 1}, 0, 64,
                                startup ? ImageRole::MainPIE
                                        : ImageRole::SharedObject,
                                bias, page, record.elf) == ElfError::None);
    record.startup = startup;
    record.ownership.~ModuleOwnership();
    new (&record.ownership) ModuleOwnership(bias, 1, startup);
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
  for (size_t i = 0; i < n; ++i) {
    if (trace[i] != values[i])
      std::fprintf(stderr, "trace[%zu]: %#lx expected %#lx\n", i,
                   static_cast<unsigned long>(trace[i]),
                   static_cast<unsigned long>(values[i]));
    check(trace[i] == values[i]);
  }
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
  {
    RuntimeInitializers fresh;
    RuntimeInitializers::Batch first(page);
    auto barriers = hooks;
    barriers.invoke_initializer = [](Address pc, int, char **, char **) {
      record(pc);
    };
    barriers.invoke_finalizer = record;
    LoaderLocks::Session session(locks, barriers);
    LoaderLocks::Guard guard(session, Kind::Mutation);
    check(!guard.error() && !fresh.prepare(startup, first, true));
    check(!fresh.attach(first, startup));
    auto call = [](Address pc, int, char **, char **) { record(pc); };
    check(!fresh.run(first, session, stack));
    check(!fresh.run(first, session, stack, call));
    const Address init[] = {0x102014, 0x102000, 0x102008, 0x10200c};
    expect(init, 4);
    trace_count = 0;
    check(!fresh.finalize(session));
    check(!fresh.finalize(session, record));
    const Address fini[] = {0x102010, 0x102004};
    expect(fini, 2);
    trace_count = 0;
  }
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

  // Detached finalization preserves actual SCC completion order, reserves
  // code through callback return, and does not retain even the mutation lock.
  {
    Input root(0x900000, true), x(0xb00000), y(0xd00000);
    const ImageRegistry::Image *xd[] = {&y.record}, *yd[] = {&x.record};
    x.record.dependencies = xd;
    y.record.dependencies = yd;
    const ImageRegistry::Image *set[] = {&root.record, &x.record, &y.record};
    ImageRegistry::Snapshot initial{1, {set, 1}}, all{2, set};
    RuntimeInitializers detached;
    RuntimeInitializers::Batch boot(page), loaded(page);
    RuntimeInitializers::FinalizationPlan plan(page), duplicate(page), exit(page);
    LoaderLocks::Session session(locks, hooks);
    {
      LoaderLocks::Guard guard(session, Kind::Mutation);
      seed(detached, boot, initial);
      check(!detached.prepare(all, loaded) && !detached.attach(loaded, all));
      check(!detached.run(loaded, session, stack,
                          [](Address, int, char **, char **) {}));
      fail_mapping = true;
      check(detached.collect_finalizers(all, plan, session) == -ENOMEM);
      fail_mapping = false;
      check(!plan.reset());
      // A later pin failure must undo earlier pins without claiming records.
      x.record.ownership.~ModuleOwnership();
      new (&x.record.ownership) ModuleOwnership(0xb00000, 1, false, 0);
      check(detached.collect_finalizers(all, plan, session) == -EOVERFLOW);
      check(!x.record.ownership.owners() && !y.record.ownership.owners());
      check(!plan.reset());
      x.record.ownership.~ModuleOwnership();
      new (&x.record.ownership) ModuleOwnership(0xb00000, 1, false);
      check(!detached.collect_finalizers(all, plan, session));
      check(x.record.ownership.owners() == 1 && y.record.ownership.owners() == 1);
      check(plan.reset() == -EBUSY);
      check(detached.invoke_finalizers(plan, session, record) == -EINVAL);
      check(detached.complete_finalizers(plan, session) == -EBUSY);
      check(!detached.collect_finalizers(all, duplicate, session));
    }
    trace_count = 0;
    Input nested_image(0xf00000);
    const ImageRegistry::Image *expanded_set[] = {
        &root.record, &x.record, &y.record, &nested_image.record};
    ImageRegistry::Snapshot expanded{3, expanded_set};
    RuntimeInitializers::Batch nested_batch(page);
    Arena scratch(page), owners(page);
    ModuleOwnership::Token external;
    struct Reentry {
      RuntimeInitializers &manager;
      RuntimeInitializers::FinalizationPlan &active;
      RuntimeInitializers::Batch &batch;
      ImageRegistry::Snapshot &snapshot;
      ImageRegistry::Image &image;
      Arena &scratch, &owners;
      ModuleOwnership::Token &external;
      InitialStack &stack;
      size_t page;
      bool entered = false;
      unsigned nested_calls = 0;
    } context{detached, plan, nested_batch, expanded, x.record, scratch,
              owners, external, stack, page};
    static Reentry *reentry;
    reentry = &context;
    auto unlocked = [](Address pc) {
      record(pc);
      auto &c = *reentry;
      if (!c.entered) {
        c.entered = true;
        LoaderLocks::Session nested(locks, hooks);
        check(c.manager.invoke_finalizers(c.active, nested, record) == -EBUSY);
        RuntimeInitializers::FinalizationPlan nested_plan(c.page);
        {
          LoaderLocks::Guard guard(nested, Kind::Mutation);
          LIBC_NAMESPACE::cpp::span<const ImageRegistry::Image *const> result;
          check(c.manager.finalization_candidates(c.active, c.snapshot,
                    c.scratch, {}, result, nested) == -EBUSY);
          check(result.empty());
          // A failed nested load must not make the outer finalizer retryable.
          RuntimeInitializers::Batch failed(c.page);
          fail_mapping = true;
          check(c.manager.prepare(c.snapshot, failed) == -ENOMEM);
          fail_mapping = false;
          check(!c.manager.prepare(c.snapshot, c.batch));
          check(!c.manager.attach(c.batch, c.snapshot));
          check(!c.manager.run(c.batch, nested, c.stack,
                              [](Address, int, char **, char **) {}));
          check(!c.manager.collect_finalizers(c.snapshot, nested_plan, nested));
        }
        check(!c.manager.invoke_finalizers(nested_plan, nested, [](Address) {
          ++reentry->nested_calls;
        }));
        {
          LoaderLocks::Guard guard(nested, Kind::Mutation);
          check(!c.manager.complete_finalizers(nested_plan, nested));
          check(!nested_plan.reset());
        }
      }
      pthread_t probe;
      check(!pthread_create(&probe, nullptr, [](void *) -> void * {
        LoaderLocks::Session other(locks, hooks);
        LoaderLocks::Guard graph(other, Kind::Mutation);
        LoaderLocks::Guard registry(other, Kind::Registry);
        LoaderLocks::Guard arena(other, Kind::Arena);
        check(!graph.error() && !registry.error() && !arena.error());
        auto &c = *reentry;
        ModuleOwnership::Token rejected;
        check(c.image.ownership.acquire(c.owners, ModuleOwnership::Kind::Handle,
                                        rejected) == -EBUSY);
        if (!c.external.ticket)
          check(!c.image.ownership.acquire(c.owners,
                    ModuleOwnership::Kind::Runtime, c.external));
        return nullptr;
      }, nullptr));
      check(!pthread_join(probe, nullptr));
    };
    check(!detached.invoke_finalizers(plan, session, unlocked));
    check(!detached.invoke_finalizers(plan, session, unlocked));
    check(!detached.invoke_finalizers(duplicate, session, unlocked));
    const Address cyclic_fini[] = {0xd02010, 0xd02004, 0xb02010, 0xb02004};
    expect(cyclic_fini, 4);
    check(context.nested_calls == 2);
    check(x.record.ownership.owners() == 2 && y.record.ownership.owners() == 1);
    {
      LoaderLocks::Guard guard(session, Kind::Mutation);
      check(!detached.complete_finalizers(plan, session));
      check(!detached.complete_finalizers(plan, session));
      check(!detached.complete_finalizers(duplicate, session));
      LIBC_NAMESPACE::cpp::span<const ImageRegistry::Image *const> candidates;
      check(!detached.finalization_candidates(plan, expanded, scratch, {},
                                               candidates, session));
      check(candidates.empty());
      check(!x.record.ownership.release(external));
      const ImageRegistry::Image *roots[] = {&x.record};
      check(!detached.finalization_candidates(plan, expanded, scratch, roots,
                                               candidates, session));
      check(candidates.empty());
      check(!detached.finalization_candidates(plan, expanded, scratch, {},
                                               candidates, session));
      check(candidates.size() == 2);
      Input pending(0x1000000);
      pending.record.dependencies = roots;
      const ImageRegistry::Image *new_set[] = {
          &root.record, &x.record, &y.record, &nested_image.record,
          &pending.record};
      ImageRegistry::Snapshot current{4, new_set};
      check(!detached.finalization_candidates(plan, current, scratch, {},
                                               candidates, session));
      check(candidates.empty());
      check(!x.record.ownership.available_for_load());
      check(!y.record.ownership.available_for_load());
      check(!detached.collect_finalizers(all, exit, session, true));
    }
    trace_count = 0;
    check(!detached.invoke_finalizers(exit, session, unlocked));
    const Address root_fini[] = {0x902010, 0x902004};
    expect(root_fini, 2);
    {
      LoaderLocks::Guard guard(session, Kind::Mutation);
      check(!detached.complete_finalizers(exit, session));
      check(!detached.finalize(session, record));
      check(!plan.reset() && !duplicate.reset() && !exit.reset());
    }
    expect(root_fini, 2);
  }

  // A NODELETE provider stays at close but still finalizes at process exit.
  // Cross-image fini pointers retain the actual provider, not just the owner.
  {
    Input root(0x1100000, true), consumer(0x1300000), provider(0x1500000);
    provider.record.ownership.~ModuleOwnership();
    new (&provider.record.ownership) ModuleOwnership(0x1500000, 1, true);
    consumer.put(0x4910, 8, 0x1502010);
    const ImageRegistry::Image *set[] = {&root.record, &consumer.record,
                                       &provider.record};
    ImageRegistry::Snapshot all{1, set};
    RuntimeInitializers selective;
    RuntimeInitializers::Batch batch(page);
    RuntimeInitializers::FinalizationPlan plan(page);
    LoaderLocks::Session session(locks, hooks);
    {
      LoaderLocks::Guard guard(session, Kind::Mutation);
      check(!selective.prepare(all, batch, true));
      check(!selective.attach(batch, all));
      check(!selective.run(batch, session, stack,
                           [](Address, int, char **, char **) {}));
      check(!selective.collect_finalizers(all, plan, session));
      check(provider.record.ownership.owners() == 1);
    }
    trace_count = 0;
    check(!selective.invoke_finalizers(plan, session, record));
    const Address selected[] = {0x1502010, 0x1302004};
    expect(selected, 2);
    {
      LoaderLocks::Guard guard(session, Kind::Mutation);
      check(!selective.complete_finalizers(plan, session) && !plan.reset());
      check(!selective.collect_finalizers(all, plan, session, true));
    }
    trace_count = 0;
    check(!selective.invoke_finalizers(plan, session, record));
    const Address exit_order[] = {0x1102010, 0x1102004, 0x1502010, 0x1502004};
    expect(exit_order, 4);
    {
      LoaderLocks::Guard guard(session, Kind::Mutation);
      check(!selective.complete_finalizers(plan, session) && !plan.reset());
    }
  }

  // A pending consumer keeps its already-completed dependency live at close.
  // Exit may finalize that dependency, but never the unfinished consumer.
  {
    Input root(0x1900000, true), dependency(0x1b00000), pending(0x1d00000);
    const ImageRegistry::Image *deps[] = {&dependency.record};
    pending.record.dependencies = deps;
    const ImageRegistry::Image *set[] = {&root.record, &dependency.record,
                                       &pending.record};
    ImageRegistry::Snapshot initial{1, {set, 1}}, first{2, {set, 2}}, all{3, set};
    RuntimeInitializers partial;
    RuntimeInitializers::Batch boot(page), loaded(page), unfinished(page);
    RuntimeInitializers::FinalizationPlan plan(page);
    LoaderLocks::Session session(locks, hooks);
    {
      LoaderLocks::Guard guard(session, Kind::Mutation);
      seed(partial, boot, initial);
      check(!partial.prepare(first, loaded) && !partial.attach(loaded, first));
      check(!partial.run(loaded, session, stack,
                         [](Address, int, char **, char **) {}));
      check(!partial.prepare(all, unfinished) && !partial.attach(unfinished, all));
      check(!partial.collect_finalizers(all, plan, session));
    }
    trace_count = 0;
    check(!partial.invoke_finalizers(plan, session, record) && !trace_count);
    {
      LoaderLocks::Guard guard(session, Kind::Mutation);
      check(!partial.complete_finalizers(plan, session) && !plan.reset());
      check(!partial.collect_finalizers(all, plan, session, true));
    }
    check(!partial.invoke_finalizers(plan, session, record));
    const Address partial_exit[] = {0x1b02010, 0x1b02004, 0x1902010, 0x1902004};
    expect(partial_exit, 4);
    {
      LoaderLocks::Guard guard(session, Kind::Mutation);
      check(!partial.complete_finalizers(plan, session) && !plan.reset());
    }
  }

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
