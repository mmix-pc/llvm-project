//===-- Runtime scope and publication tests -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/runtime_scopes.h"
#include "loader/linux/syscall.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace loader_test;
using LIBC_NAMESPACE::cpp::Atomic;
using Scopes = RuntimeScopes;
using Image = Scopes::Image;
static int fail_after = -1;
static bool fail_unmap;
static unsigned allocations, releases;
static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    fprintf(stderr, "runtime scopes check at %d\n", line);
    abort();
  }
}
template <size_t N> static Bytes text(const char (&s)[N]) {
  return {reinterpret_cast<const unsigned char *>(s), N - 1};
}

namespace LIBC_NAMESPACE::loader {
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  if (n == SYS_mmap) {
    if (!fail_after)
      return -ENOMEM;
    if (fail_after > 0)
      --fail_after;
    long result = ::syscall(n, a, b, c, d, e, f);
    if (result < 0)
      return -errno;
    ++allocations;
    return result;
  }
  check(n == SYS_munmap);
  if (fail_unmap)
    return -EIO;
  if (::munmap(reinterpret_cast<void *>(a), b))
    return -errno;
  ++releases;
  return 0;
}
} // namespace LIBC_NAMESPACE::loader

struct Input : DynamicFixture {
  Image record{};
  explicit Input(unsigned id, const char *name = "foo",
                 unsigned binding = STB_GLOBAL,
                 unsigned visibility = STV_DEFAULT, unsigned section = 1,
                 unsigned type = STT_FUNC)
      : DynamicFixture(id == 1 ? ImageRole::MainPIE : ImageRole::SharedObject) {
    // Three-character names preserve the fixture's other string offsets.
    uint32_t hash = 5381;
    for (unsigned i = 0; i < 3; ++i) {
      check(name[i]);
      bytes[0x701 + i] = name[i];
      hash = hash * 33 + static_cast<unsigned char>(name[i]);
    }
    check(!name[3]);
    put(0xa10, 8,
        (Address(1) << (hash % 64)) | (Address(1) << ((hash >> 5) % 64)));
    put(0xa1c, 4, hash | 1);
    bytes[0x81c] = (binding << 4) | type;
    bytes[0x81d] = visibility;
    put(0x81e, 2, section);
    put(0x820, 8, section == SHN_UNDEF || section == SHN_ABS ? 0 : 0x2000);
    if (type == STT_TLS) {
      segment(7, PT_TLS, PF_R, 0x4800, 0x4800, 8, 16, 8);
      put(0x820, 8, 0);
      // A TLS symbol cannot be referenced by this ordinary relocation.
      set(DT_RELASZ, 24);
    }
    check(ElfImage::open_file(
              view(), id == 1 ? ImageRole::MainPIE : ImageRole::SharedObject,
              bias + id * 0x100000, page, record.elf) == ElfError::None);
    check(DynamicInfo::open(record.elf, record.dynamic) == DynamicError::None);
    record.startup = id <= 2;
    record.discovered = true;
    record.tls.module_id = id;
    record.tls.generation = id;
  }
};

struct Reader {
  Scopes *scopes;
  const Image *caller;
  const Image *new_image;
  Atomic<unsigned> ready{0}, epoch{0}, acknowledgements{0};
  Atomic<bool> done{false};
};
static void *read_snapshots(void *opaque) {
  auto &shared = *static_cast<Reader *>(opaque);
  shared.ready.fetch_add(1);
  unsigned last = 0;
  do {
    unsigned epoch = shared.epoch.load();
    auto lease = shared.scopes->acquire();
    const auto *snapshot = lease.get();
    Scopes::Value value;
    check(snapshot && snapshot->global.images.size() >= 2);
    check(Scopes::lookup_default(*snapshot, shared.caller, text("foo"),
                                 value) == SymbolError::None);
    check(value.image == snapshot->registry->images[1]);
    bool visible = snapshot->global.contains(shared.new_image);
    SymbolError found =
        Scopes::lookup_default(*snapshot, shared.caller, text("zzz"), value);
    check(visible
              ? found == SymbolError::None && value.image == shared.new_image
              : found == SymbolError::Undefined);
    check(bool(snapshot->context(shared.new_image)) == visible);
    for (const auto &handle : snapshot->handles) {
      check(handle.token && handle.local && !handle.local->images.empty());
      for (const Image *image : handle.local->images)
        check(snapshot->context(image));
    }
    if (epoch != last) {
      last = epoch;
      shared.acknowledgements.fetch_add(1);
    }
  } while (!shared.done.load());
  return nullptr;
}

struct RetiringReader {
  Scopes &scopes;
  uint64_t handle;
  Atomic<unsigned> phase{0};
};
static void *read_retiring(void *opaque) {
  auto &shared = *static_cast<RetiringReader *>(opaque);
  auto lease = shared.scopes.acquire();
  shared.phase.store(1);
  while (shared.phase.load() != 2)
    sched_yield();
  Scopes::Value value;
  check(Scopes::lookup(*lease.get(), shared.handle, text("bar"), value) ==
        SymbolError::None);
  // An already leased operation can finish; a fresh reader cannot rediscover.
  auto fresh = shared.scopes.acquire();
  check(fresh.get()->registry->images.size() == 1);
  check(Scopes::lookup_default(*fresh.get(), fresh.get()->registry->images[0],
                               text("bar"), value) == SymbolError::Undefined);
  shared.phase.store(3);
  while (shared.phase.load() != 4)
    sched_yield();
  return nullptr;
}

static void retirement_tests() {
  Input main(1), late(3, "bar");
  const Image *images[] = {&main.record, &late.record};
  ImageRegistry::Snapshot first{1, {images, 1}}, both{2, images};
  Scopes scopes(page);
  Scopes::Transaction startup(page), open(page);
  check(!scopes.prepare_startup(first, startup) && !scopes.publish(startup));
  // Validation belongs to the image, not each independently retired scope.
  check(scopes.snapshot()->global.objects[0].metadata == main.record.metadata());
  uint64_t token;
  check(!scopes.prepare_open(both, &late.record, true, open, token) &&
        !scopes.publish(open));
  for (const auto &object : scopes.snapshot()->global.objects)
    check(object.metadata == (object.image == &main.record.elf
                                 ? main.record.metadata()
                                 : late.record.metadata()));
  const Image *retired[] = {&late.record};
  Scopes::Transaction rejected(page);
  check(scopes.prepare_retire(retired, rejected) == -EBUSY);
  check(!rejected.abort());
  RetiringReader reader{scopes, token};
  pthread_t thread;
  check(!pthread_create(&thread, nullptr, read_retiring, &reader));
  while (reader.phase.load() != 1)
    sched_yield();
  Scopes::Transaction close(page), retire(page);
  check(!scopes.prepare_close(token, close) && !scopes.publish(close));
  check(scopes.snapshot()->global.objects[0].metadata == main.record.metadata());
  Scopes::Transaction failed(page);
  const auto *before = scopes.snapshot();
  fail_after = 0;
  check(scopes.prepare_retire(retired, failed) == -ENOMEM);
  fail_after = -1;
  check(!failed.abort() && scopes.snapshot() == before &&
        late.record.ownership.admission() == ModuleOwnership::State::Live);
  const Image *needed[] = {&late.record};
  main.record.dependencies = needed;
  check(scopes.prepare_retire(retired, failed) == -EBUSY);
  check(!failed.abort());
  main.record.dependencies = {};
  ImageRegistry::Binding binding{&late.record, nullptr};
  main.record.bindings = &binding;
  check(scopes.prepare_retire(retired, failed) == -EBUSY);
  check(!failed.abort());
  main.record.bindings = nullptr;
  check(!scopes.prepare_retire(retired, retire));
  check(late.record.ownership.admission() == ModuleOwnership::State::Live);
  check(scopes.collect_snapshots() == -EBUSY); // Prepared writer borrows base.
  check(!late.record.ownership.retain_signal());
  check(!scopes.can_publish(retire) && scopes.publish(retire) == -EBUSY);
  check(scopes.snapshot() == before &&
        late.record.ownership.admission() == ModuleOwnership::State::Live);
  late.record.ownership.finish_signal(false);
  check(!scopes.publish(retire));
  check(late.record.ownership.retain_signal() == -EBUSY);
  check(late.record.ownership.admission() == ModuleOwnership::State::Retiring);
  check(scopes.snapshot()->registry->images.size() == 1);
  reader.phase.store(2);
  while (reader.phase.load() != 3)
    sched_yield();
  check(scopes.collect_snapshots() == -EBUSY);
  check(!scopes.readers_drained(&late.record));
  reader.phase.store(4);
  check(!pthread_join(thread, nullptr) && !scopes.active_readers());
  check(scopes.readers_drained(&late.record));
  fail_unmap = true;
  check(scopes.collect_snapshots() == -EIO);
  fail_unmap = false;
  check(!scopes.collect_snapshots());
  check(scopes.prepare_open(both, &late.record, false, rejected, token) ==
        -EBUSY);
  check(!rejected.abort());

  auto failed_lookup = [&]() {
    auto lease = scopes.acquire();
    Scopes::Value value;
    return Scopes::lookup(*lease.get(), 0, {}, value);
  };
  check(failed_lookup() == SymbolError::Scope && !scopes.active_readers());
  // Repeated publication reuses controls and releases historical arrays. Warm
  // up both slots before asserting bounded live arena mappings.
  unsigned steady = 0;
  for (unsigned i = 0; i < 8; ++i) {
    Scopes::Transaction opened(page), closed(page);
    check(!scopes.prepare_open(*scopes.snapshot()->registry, nullptr, false,
                               opened, token));
    check(!scopes.publish(opened));
    check(!scopes.prepare_close(token, closed) && !scopes.publish(closed));
    check(!scopes.collect_snapshots());
    if (i == 2)
      steady = allocations - releases;
    if (i > 2)
      check(allocations - releases == steady);
  }
}

extern "C" int main() {
  Input main(1, "foo", STB_GLOBAL, STV_DEFAULT, SHN_UNDEF);
  Input global(2, "foo", STB_WEAK), a(3),
      dependency(4, "foo", STB_GLOBAL, STV_PROTECTED);
  Input b(5, "bar"),
      zero(6, "nil", STB_GLOBAL, STV_DEFAULT, SHN_ABS, STT_OBJECT);
  Input weak(7, "zzz", STB_WEAK, STV_DEFAULT, SHN_UNDEF);
  Input hidden(8, "foo", STB_GLOBAL, STV_HIDDEN),
      tls(9, "tls", STB_GLOBAL, STV_DEFAULT, 1, STT_TLS);
  Input newly(10, "zzz");
  const Image *all[] = {&main.record,       &global.record, &a.record,
                        &dependency.record, &b.record,      &zero.record,
                        &weak.record,       &hidden.record, &tls.record,
                        &newly.record};
  const Image *a_deps[] = {&dependency.record}, *dep_deps[] = {&a.record};
  a.record.dependencies = a_deps;
  dependency.record.dependencies = dep_deps;
  ImageRegistry::Snapshot registries[7] = {
      {1, {all, 2}}, {2, {all, 4}}, {3, {all, 5}}, {4, {all, 6}},
      {5, {all, 8}}, {6, {all, 9}}, {7, {all, 10}}};
  Scopes scopes(4096);
  Scopes::Transaction startup(4096);
  check(!scopes.prepare_startup(registries[0], startup));
  check(!scopes.snapshot() && !scopes.publish(startup));
  const auto *initial = scopes.snapshot();
  Scopes::Value value;
  uint64_t ta, tb, tz, tw, th, tt, null_handle;
  auto open = [&](const ImageRegistry::Snapshot &registry, const Image *root,
                  bool promote, uint64_t &token) {
    Scopes::Transaction transaction(4096);
    check(!scopes.prepare_open(registry, root, promote, transaction, token));
    check(!scopes.publish(transaction));
  };
  open(registries[0], nullptr, false, null_handle);
  open(registries[1], &a.record, false, ta);
  const auto *local = scopes.snapshot();
  check(local->global.images.size() == 2);
  check(Scopes::lookup(*local, ta, text("foo"), value) == SymbolError::None &&
        value.image == &a.record);
  check(Scopes::lookup_default(*local, &a.record, text("foo"), value) ==
            SymbolError::None &&
        value.image == &global.record);
  check(Scopes::lookup_next(*local, &a.record, text("foo"), value) ==
            SymbolError::None &&
        value.image == &dependency.record);
  check(Scopes::lookup_next(*local, &global.record, text("foo"), value) ==
        SymbolError::Undefined);
  check(Scopes::resolve_binding(*local, &a.record, 1, value) ==
            SymbolError::None &&
        value.image == &global.record);
  check(Scopes::resolve_binding(*local, &dependency.record, 1, value) ==
            SymbolError::None &&
        value.image == &dependency.record);
  open(registries[2], &b.record, false, tb);
  check(Scopes::lookup_default(*scopes.snapshot(), &main.record, text("bar"),
                               value) == SymbolError::Undefined);
  check(Scopes::lookup_default(*scopes.snapshot(), &b.record, text("bar"),
                               value) == SymbolError::None &&
        value.image == &b.record);
  check(Scopes::lookup(*scopes.snapshot(), ta, text("bar"), value) ==
        SymbolError::Undefined);
  check(Scopes::lookup(*scopes.snapshot(), null_handle, text("bar"), value) ==
        SymbolError::Undefined);
  uint64_t promoted;
  open(registries[2], &b.record, true, promoted);
  const uint64_t global_b = promoted;
  check(promoted != tb);
  check(Scopes::lookup(*scopes.snapshot(), null_handle, text("bar"), value) ==
        SymbolError::None);
  check(Scopes::lookup_default(*local, &main.record, text("bar"), value) ==
        SymbolError::Undefined);
  open(registries[2], &a.record, true, promoted);
  const uint64_t global_a = promoted;
  check(scopes.snapshot()->global.images.size() == 5);
  check(Scopes::lookup_next(*scopes.snapshot(), &global.record, text("foo"),
                            value) == SymbolError::None &&
        value.image == &a.record);
  check(Scopes::resolve_binding(*scopes.snapshot(), &a.record, 1, value) ==
            SymbolError::None &&
        value.image == &global.record);
  open(registries[3], &zero.record, true, tz);
  check(Scopes::lookup(*scopes.snapshot(), tz, text("nil"), value) ==
            SymbolError::None &&
        !value.symbol.address && !value.symbol.weak_zero &&
        value.image == &zero.record);

  // Append both hidden/weak records as one local graph, without exporting
  // either.
  const Image *weak_deps[] = {&hidden.record};
  weak.record.dependencies = weak_deps;
  open(registries[4], &weak.record, false, tw);
  open(registries[4], &hidden.record, false, th);
  check(Scopes::resolve_binding(*scopes.snapshot(), &weak.record, 1, value) ==
            SymbolError::None &&
        value.symbol.weak_zero && !value.symbol.address);
  retirement_tests();
  check(Scopes::lookup(*scopes.snapshot(), tw, text("zzz"), value) ==
        SymbolError::Undefined);
  check(Scopes::lookup(*scopes.snapshot(), th, text("foo"), value) ==
        SymbolError::Undefined);
  check(Scopes::resolve_binding(*scopes.snapshot(), &hidden.record, 1, value) ==
            SymbolError::None &&
        value.image == &hidden.record);
  open(registries[5], &tls.record, false, tt);
  check(Scopes::lookup(*scopes.snapshot(), tt, text("tls"), value) ==
            SymbolError::None &&
        value.symbol.tls && !value.symbol.address &&
        value.image == &tls.record);
  check(Scopes::lookup(*scopes.snapshot(), tt, {}, value) ==
        SymbolError::Index);
  check(Scopes::lookup(*scopes.snapshot(), tt, text("t\0s"), value) ==
        SymbolError::Index);
  check(Scopes::lookup(*scopes.snapshot(), 0, text("foo"), value) ==
        SymbolError::Scope);
  check(Scopes::lookup(*scopes.snapshot(), ~uint64_t(0), text("foo"), value) ==
        SymbolError::Scope);
  check(Scopes::lookup_next(*scopes.snapshot(),
                            reinterpret_cast<const Image *>(1), text("foo"),
                            value) == SymbolError::Scope);
  check(Scopes::lookup_default(*scopes.snapshot(), nullptr, text("foo"),
                               value) == SymbolError::Scope);
  const Image *caller;
  check(Scopes::caller(*scopes.snapshot(), a.record.elf.load_bias() + 0x2000,
                       caller) == SymbolError::None &&
        caller == &a.record);
  check(Scopes::caller(*scopes.snapshot(), a.record.elf.load_bias() + 0x4000,
                       caller) == SymbolError::Scope &&
        !caller);
  check(Scopes::caller(*scopes.snapshot(), a.record.elf.load_bias() + 0x2001,
                       caller) == SymbolError::Scope);
  check(Scopes::caller(*scopes.snapshot(), 0, caller) == SymbolError::Scope);
  Image collision{main.record.elf, main.record.tls, main.record.startup};
  const Image *overlapping[] = {&main.record, &collision};
  ImageRegistry::Snapshot conflict{1, overlapping};
  Scopes::Snapshot ambiguous = *initial;
  ambiguous.registry = &conflict;
  check(Scopes::caller(ambiguous, main.record.elf.load_bias() + 0x2000,
                       caller) == SymbolError::Scope &&
        !caller);

  const auto *before_close = scopes.snapshot();
  Scopes::Transaction close(4096);
  check(!scopes.prepare_close(tb, close) && !scopes.publish(close));
  Scopes::Transaction close_global(4096);
  check(!scopes.prepare_close(global_b, close_global) &&
        !scopes.publish(close_global));
  check(Scopes::lookup(*scopes.snapshot(), tb, text("bar"), value) ==
        SymbolError::Scope);
  check(Scopes::lookup(*before_close, tb, text("bar"), value) ==
        SymbolError::None);
  check(Scopes::lookup_default(*scopes.snapshot(), &main.record, text("bar"),
                               value) == SymbolError::None);
  Scopes::Transaction invalid(4096);
  check(scopes.prepare_close(tb, invalid) == -EINVAL);
  check(scopes.prepare_open(registries[5], reinterpret_cast<const Image *>(1),
                            false, invalid, promoted) == -EINVAL);
  check(scopes.prepare_open(registries[0], nullptr, false, invalid, promoted) ==
        -EINVAL);
  check(!invalid.abort());

  // Closing all explicit references to a dependency cycle neither collects
  // its images nor revokes retained binding scopes or global promotion.
  const auto *cycle_snapshot = scopes.snapshot();
  const uint64_t cycle_tokens[] = {ta, global_a};
  for (uint64_t token : cycle_tokens) {
    Scopes::Transaction cycle_close(4096);
    check(!scopes.prepare_close(token, cycle_close) &&
          !scopes.publish(cycle_close));
  }
  check(scopes.snapshot()->registry == cycle_snapshot->registry);
  check(Scopes::lookup_next(*scopes.snapshot(), &a.record, text("foo"), value) ==
            SymbolError::None && value.image == &dependency.record);
  uint64_t reopened_cycle;
  open(registries[5], &a.record, false, reopened_cycle);
  check(reopened_cycle != ta && reopened_cycle != global_a);
  check(Scopes::lookup(*scopes.snapshot(), reopened_cycle, text("foo"), value) ==
            SymbolError::None && value.image == &a.record);

  Scopes::Transaction stale(4096);
  uint64_t stale_token;
  check(!scopes.prepare_open(registries[5], &b.record, false, stale,
                             stale_token));
  open(registries[5], &b.record, true, promoted);
  check(scopes.publish(stale) == -EAGAIN && !stale.abort());
  check(promoted != stale_token);
  check(initial->global.images.size() == 2 && initial->handles.empty());

  // Partial allocation never changes the published snapshot; failed unmaps
  // block reuse until abort succeeds. Tokens from aborted preparation stay
  // dead.
  const auto *before_failure = scopes.snapshot();
  Scopes::Transaction measured(4096);
  unsigned before = allocations;
  check(
      !scopes.prepare_open(registries[5], &b.record, true, measured, promoted));
  unsigned required = allocations - before;
  check(!measured.abort());
  for (unsigned i = 0; i < required; ++i) {
    Scopes::Transaction failed(4096);
    unsigned live = allocations - releases;
    fail_after = i;
    check(scopes.prepare_open(registries[5], &b.record, true, failed,
                              promoted) == -ENOMEM);
    fail_after = -1;
    check(!failed.view() && scopes.snapshot() == before_failure);
    if (allocations - releases != live) {
      fail_unmap = true;
      check(failed.abort() == -EIO);
      check(scopes.prepare_open(registries[5], &b.record, false, failed,
                                promoted) == -EBUSY);
      fail_unmap = false;
    }
    check(!failed.abort() && allocations - releases == live);
  }

  Reader readers{&scopes, &main.record, &newly.record};
  pthread_t threads[4];
  for (auto &thread : threads)
    check(!pthread_create(&thread, nullptr, read_snapshots, &readers));
  while (readers.ready.load() != 4)
    sched_yield();
  for (unsigned i = 1; i <= 8; ++i) {
    open(registries[6], i == 1 ? &newly.record : &b.record, true, promoted);
    readers.epoch.store(i);
    while (readers.acknowledgements.load() != i * 4)
      sched_yield();
    long collected = scopes.collect_snapshots();
    check(!collected || collected == -EBUSY);
  }
  readers.done.store(true);
  for (auto &thread : threads)
    check(!pthread_join(thread, nullptr));
  check(!scopes.collect_snapshots());
  // New global definitions affect future queries, not previously bound weak
  // references. Recomputing the old binding against the new global scope would
  // incorrectly replace its committed zero value.
  check(Scopes::lookup_default(*scopes.snapshot(), &weak.record, text("zzz"),
                               value) == SymbolError::None &&
        value.image == &newly.record);
  check(Scopes::resolve_binding(*scopes.snapshot(), &weak.record, 1, value) ==
            SymbolError::None &&
        value.symbol.weak_zero && !value.symbol.address);
  // Closing admission does not block unrelated opens. Reopening any member
  // of a dependency closure containing the finalized image does fail.
  dependency.record.ownership.begin_finalization();
  Scopes::Transaction blocked(page), unrelated(page);
  uint64_t denied = 0, allowed = 0;
  check(scopes.prepare_open(registries[6], &dependency.record, false, blocked, denied) ==
        -EBUSY);
  check(!denied && !blocked.abort());
  check(scopes.prepare_open(registries[6], &a.record, false, blocked, denied) ==
        -EBUSY);
  check(!denied && !blocked.abort());
  check(!scopes.prepare_open(registries[6], &newly.record, false, unrelated,
                             allowed));
  check(allowed && !unrelated.abort());

  puts("runtime lookup scopes, retained binding and snapshot publication "
       "passed");
  return 0;
}
