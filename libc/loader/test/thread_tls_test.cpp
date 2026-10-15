//===-- Runtime thread TLS lifecycle tests --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/syscall.h"
#include "loader/linux/thread_tls.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace loader_test;
static Address tp, mask;
static unsigned maps, unmaps, fail_map, fail_unmap;
static bool fail_all_unmaps;
struct Mapping {
  Address base, size;
};
static Mapping live[128];
static unsigned live_count;
static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    std::fprintf(stderr, "thread TLS check at %d\n", line);
    std::abort();
  }
}
namespace LIBC_NAMESPACE::loader {
Address current_thread_pointer() { return tp; }
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  if (n == SYS_rt_sigprocmask) {
    check(d == 8);
    if (c)
      *reinterpret_cast<Address *>(c) = mask;
    mask = a == SIG_BLOCK ? ~Address(0) : *reinterpret_cast<Address *>(b);
    return 0;
  }
  if (n == SYS_mmap) {
    if (++maps == fail_map)
      return -ENOMEM;
    long result = ::syscall(n, a, b, c, d, e, f);
    check(result > 0 && live_count < 128);
    live[live_count++] = {Address(result), Address(b)};
    return result;
  }
  if (n == SYS_munmap) {
    if (++unmaps == fail_unmap || fail_all_unmaps)
      return -EIO;
    unsigned i = 0;
    while (i < live_count && live[i].base != Address(a))
      ++i;
    check(i < live_count && live[i].size == Address(b));
    check(!::munmap(reinterpret_cast<void *>(a), b));
    live[i] = live[--live_count];
    return 0;
  }
  long result = ::syscall(n, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
} // namespace LIBC_NAMESPACE::loader

alignas(8192) static unsigned char memory[2][0x6000];
struct FixtureState {
  DynamicFixture files[2] = {DynamicFixture(ImageRole::MainPIE),
                             DynamicFixture()};
  MappedRegion regions[2];
  ImageRegistry::Image records[2]{};
  const ImageRegistry::Image *images[2] = {&records[0], &records[1]};
  ImageRegistry::Snapshot initial{1, {images, 1}}, late{2, images};
  StartupTLSModule module;
  StartupTLSSet layout;
  RelocatedTLS ready;
  RuntimeScopes scopes{page};
  ThreadTLSContext context{{&ready, page}, &scopes};
  FixtureState() {
    for (unsigned i = 0; i < 2; ++i) {
      files[i].segment(7, PT_TLS, PF_R, 0x4a08, 0x4a08, 16, 64, 32);
      std::memcpy(memory[i], files[i].bytes, sizeof(memory[i]));
      regions[i] = {0, {memory[i], sizeof(memory[i])}};
      check(ElfImage::open_mapped({regions + i, 1}, 0, 64,
                                  i ? ImageRole::SharedObject
                                    : ImageRole::MainPIE,
                                  reinterpret_cast<Address>(memory[i]), page,
                                  records[i].elf) == ElfError::None);
      records[i].startup = !i;
      records[i].discovered = true;
    }
    const ElfImage *startup[] = {&records[0].elf};
    check(plan_startup_tls(startup, {&module, 1}, layout) ==
          TLSLayoutError::None);
    DynamicInfo metadata;
    check(DynamicInfo::open(records[0].elf, metadata) == DynamicError::None);
    SymbolObject object{&records[0].elf, &metadata};
    WritableRegion writable{0x4000, {memory[0] + 0x4000, 0x2000}};
    LIBC_NAMESPACE::cpp::span<const WritableRegion> writes{&writable, 1};
    check(
        relocate_startup_tls({&object, 1}, {&writes, 1}, layout, ready).error ==
        StartupRelocationError::None);
    records[0].tls = module;
    Address cursor = 0;
    check(place_tls_module(records[1].elf, cursor, records[1].tls));
    records[1].tls.module_id = 3;
    records[1].tls.generation = 2;
    records[1].tls.tp_offset = 0;
    RuntimeScopes::Transaction tx(page);
    check(!scopes.prepare_startup(initial, tx) && !scopes.publish(tx));
  }
  void publish() {
    RuntimeScopes::Transaction tx(page);
    uint64_t token;
    check(!scopes.prepare_open(late, images[1], false, tx, token));
    check(!scopes.publish(tx));
  }
  unsigned char *lookup(ThreadTLSAllocation &a, unsigned id) {
    tp = a.tp;
    TLSIndex index{id, 0};
    return static_cast<unsigned char *>(
        resolve_runtime_thread_tls(&context, &index));
  }
};

extern "C" int main() {
  FixtureState f;
  ThreadTLSAllocation old{}, newly{};
  check(!prepare_runtime_thread_tls(&f.context, &f, &old));
  check(prepare_runtime_thread_tls(&f.context, &f, &old) == EBUSY);
  auto *startup = f.lookup(old, 1);
  *startup = 19;
  check(release_runtime_thread_tls(&f.context, &old) == EBUSY);
  tp = 0;
  // Publication between preparation and first worker execution. Both the old
  // and new worker must see the new module without moving startup storage.
  f.publish();
  check(!prepare_runtime_thread_tls(&f.context, &f, &newly));
  check(reinterpret_cast<StartupDTV *>(newly.dtv_mapping_base)->generation ==
        2);
  auto *a = f.lookup(old, 3);
  *a = 42;
  auto *b = f.lookup(newly, 3);
  check(a != b && !*b);
  *b = 73;
  check(f.lookup(old, 3) == a && *a == 42);
  check(f.lookup(old, 1) == startup && *startup == 19);
  check(f.lookup(newly, 3) == b && *b == 73);
  // Cleanup/TSS can still use TLS. Only afterward may terminal/reference drain
  // transfer a copied descriptor to an exclusive reclaimer on another TP.
  ThreadTLSAllocation reclaimer = old;
  tp = 0;
  fail_unmap = unmaps + 1;
  check(release_runtime_thread_tls(&f.context, &reclaimer) == EIO);
  check(reclaimer.runtime_owner && !reclaimer.tp);
  fail_unmap = 0;
  check(!release_runtime_thread_tls(&f.context, &reclaimer));
  check(!reclaimer.runtime_owner && !reclaimer.dtv_mapping_size);
  check(!release_runtime_thread_tls(&f.context, &reclaimer));
  // A failed owner-state unmap must survive even after all TLS is released.
  fail_unmap = unmaps + 4; // Late image, DTV, common block, owner state.
  check(release_runtime_thread_tls(&f.context, &newly) == EIO);
  check(newly.runtime_owner && !newly.tls_mapping_size &&
        !newly.dtv_mapping_size);
  fail_unmap = 0;
  check(!release_runtime_thread_tls(&f.context, &newly));
  unsigned resident = live_count;
  for (unsigned failure = 1; failure <= 4; ++failure) {
    ThreadTLSAllocation failed{};
    fail_map = maps + failure; // Owner, common block, DTV, expanded DTV.
    check(prepare_runtime_thread_tls(&f.context, &f, &failed) == ENOMEM);
    fail_map = 0;
    check(!release_runtime_thread_tls(&f.context, &failed));
    check(live_count == resident);
  }
  {
    ThreadTLSAllocation failed{};
    fail_map = maps + 4;
    fail_unmap = unmaps + 1;
    check(prepare_runtime_thread_tls(&f.context, &f, &failed) == ENOMEM);
    check(failed.runtime_owner);
    fail_map = fail_unmap = 0;
    check(!release_runtime_thread_tls(&f.context, &failed));
    check(live_count == resident);
  }
  // Clone rejection uses exactly the unpublished release path; no TLS lookup
  // or current-TP refresh is required for rollback on the creator's thread.
  ThreadTLSAllocation aborted{};
  check(!prepare_runtime_thread_tls(&f.context, &f, &aborted));
  check(!release_runtime_thread_tls(&f.context, &aborted));
  check(live_count == resident);
  // A growth publication may retain an obsolete DTV if munmap fails. Both
  // that table and the new table must survive rollback for a later reclaimer.
  fail_all_unmaps = true;
  check(prepare_runtime_thread_tls(&f.context, &f, &aborted) == EIO);
  check(aborted.runtime_owner && live_count > resident);
  fail_all_unmaps = false;
  check(!release_runtime_thread_tls(&f.context, &aborted));
  check(live_count == resident);
  std::puts("runtime TLS prepare, publication, copied retirement and rollback "
            "passed");
  return 0;
}
