//===-- Runtime unwind publication tests ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/runtime_unwind.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace loader_test;
using LIBC_NAMESPACE::cpp::Atomic;
static bool fail_mapping;
static thread_local bool lookup_only;
static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    std::fprintf(stderr, "runtime unwind check at %d\n", line);
    std::abort();
  }
}
namespace LIBC_NAMESPACE::loader {
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  check(!lookup_only); // Readers must not allocate, lock via futex or do I/O.
  if (fail_mapping && n == SYS_mmap)
    return -ENOMEM;
  long result = ::syscall(n, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
[[noreturn]] void fatal_startup() { _exit(127); }
} // namespace LIBC_NAMESPACE::loader

struct Input : DynamicFixture {
  MappedRegion region;
  ImageRegistry::Image record{};
  Input(ImageRole role, Address bias, bool frames = true)
      : DynamicFixture(role) {
    put(56, 2, 9);
    segment(0, PT_PHDR, PF_R, 64, 64, 9 * 56, 9 * 56, 8);
    if (frames) {
      segment(8, PT_GNU_EH_FRAME, PF_R, 0xc00, 0xc00, 20, 20, 4);
      put(0xc00, 4, 0x011b033b);
      put(0xc04, 4, 0xd00 - 0xc04);
      put(0xc08, 4, 1);
      put(0xc0c, 4, 0x2000 - 0xc00);
      put(0xc10, 4, 0xd00 - 0xc00);
    }
    region = {0, view()};
    check(ElfImage::open_mapped({&region, 1}, 0, 64, role, bias, page,
                                record.elf) == ElfError::None);
    check(DynamicInfo::open(record.elf, record.dynamic) == DynamicError::None);
    record.discovered = true;
    record.startup = role == ImageRole::MainPIE;
  }
};

struct Readers {
  RuntimeScopes &scopes;
  Atomic<unsigned> ready{0}, epoch{0}, done{0};
};
struct LeaseReader {
  RuntimeScopes &scopes;
  Atomic<unsigned> ready{0}, finish{0};
};
static void *hold_lease(void *opaque) {
  auto &reader = *static_cast<LeaseReader *>(opaque);
  lookup_only = true;
  UnwindImageView original{}, copy{};
  check(!acquire_runtime_unwind_image(&reader.scopes, 0x302000, &original));
  check(!retain_runtime_unwind_image(&reader.scopes, original.token, &copy));
  check(copy.token != original.token);
  check(!drop_runtime_unwind_image(&reader.scopes, original.token));
  reader.ready.fetch_add(1);
  while (!reader.finish.load())
    sched_yield();
  check(copy.eh_frame_header == 0x300c00);
  UnwindImageView borrowed{};
  check(!reader.scopes.unwind_leases.lookup(copy.token, 0x302000, borrowed));
  check(borrowed.token == copy.token);
  const auto *ranges = reinterpret_cast<const UnwindReadableRange *>(
      copy.readable_ranges);
  check(copy.range_count == 3 && ranges[0].address == 0x300000);
  check(!drop_runtime_unwind_image(&reader.scopes, copy.token));
  check(drop_runtime_unwind_image(&reader.scopes, copy.token) == EINVAL);
  lookup_only = false;
  return nullptr;
}

static void checked_leases() {
  Input main(ImageRole::MainPIE, 0x100000);
  Input late(ImageRole::SharedObject, 0x300000);
  const ImageRegistry::Image *images[] = {&main.record, &late.record};
  ImageRegistry::Snapshot initial{1, {images, 1}}, registry{2, images};
  RuntimeScopes scopes(page);
  RuntimeScopes::Transaction startup(page);
  check(!scopes.prepare_startup(initial, startup));
  check(!scopes.prepare_unwind(startup) && !scopes.publish(startup));
  RuntimeScopes::Transaction open(page), close(page);
  uint64_t handle;
  check(!scopes.prepare_open(registry, &late.record, false, open, handle));
  check(!scopes.prepare_unwind(open) && !scopes.publish(open));
  check(!scopes.prepare_close(handle, close));
  check(!scopes.prepare_unwind(close) && !scopes.publish(close));
  UnwindImageView views[64]{}, copy{};
  lookup_only = true;
  for (auto &view : views)
    check(!acquire_runtime_unwind_image(&scopes, 0x302000, &view));
  check(acquire_runtime_unwind_image(&scopes, 0x302000, &copy) == EAGAIN);
  check(!copy.token && scopes.active_readers() == 64);
  check(retain_runtime_unwind_image(&scopes, views[0].token, &copy) == EAGAIN);
  check(!copy.token && scopes.active_readers() == 64);
  uintptr_t stale = views[0].token;
  for (auto &view : views)
    check(!drop_runtime_unwind_image(&scopes, view.token));
  check(!scopes.active_readers());
  check(!acquire_runtime_unwind_image(&scopes, 0x302000, &copy));
  check(copy.token != stale);
  check(drop_runtime_unwind_image(&scopes, stale) == EINVAL);
  check(!drop_runtime_unwind_image(&scopes, copy.token));
  check(retain_runtime_unwind_image(&scopes, stale, &copy) == EINVAL);
  check(!copy.token);
  lookup_only = false;

  LeaseReader reader{scopes};
  pthread_t workers[2];
  for (auto &worker : workers)
    check(!pthread_create(&worker, nullptr, hold_lease, &reader));
  while (reader.ready.load() != 2)
    sched_yield();
  RuntimeScopes::Transaction retire(page);
  const ImageRegistry::Image *removed[] = {&late.record};
  check(!scopes.prepare_retire(removed, retire));
  check(!scopes.prepare_unwind(retire) && !scopes.publish(retire));
  check(acquire_runtime_unwind_image(&scopes, 0x302000, &copy) == ENOENT);
  check(!copy.token && !scopes.readers_drained(&late.record));
  check(scopes.collect_snapshots() == -EBUSY);
  reader.finish.store(1);
  for (auto &worker : workers)
    check(!pthread_join(worker, nullptr));
  check(scopes.readers_drained(&late.record));
  check(!scopes.collect_snapshots());
  check(!acquire_runtime_unwind_image(&scopes, 0x102000, &copy));
  check(!scopes.unwind_leases.pin(copy.token));
  check(!scopes.active_readers());
  check(drop_runtime_unwind_image(&scopes, copy.token) == EINVAL);
  UnwindImageView resident_view{};
  check(!scopes.snapshot()->unwind->images.find(0x102000, resident_view));
  check(scopes.owns_unwind_token(resident_view.token));
}
static void *read(void *opaque) {
  auto &shared = *static_cast<Readers *>(opaque);
  lookup_only = true;
  UnwindImageView view;
  check(!find_runtime_unwind_image(&shared.scopes, 0x102000, &view));
  Address old_token = view.token;
  shared.ready.fetch_add(1);
  for (unsigned epoch = 1; epoch <= 2; ++epoch) {
    // Lookup is exercised while the writer prepares a private candidate.
    while (shared.epoch.load() < epoch) {
      check(!find_runtime_unwind_image(&shared.scopes, 0x102000, &view));
      release_runtime_unwind_image(&shared.scopes, view.token);
      sched_yield();
    }
    auto lease = shared.scopes.acquire();
    const auto *snapshot = lease.get();
    check(snapshot->registry->images.size() == epoch + 1);
    check(snapshot->unwind->registry == snapshot->registry);
    check(!find_runtime_unwind_image(&shared.scopes, 0x302000, &view));
    check(view.eh_frame_header == 0x300c00 && view.range_count == 3);
    release_runtime_unwind_image(&shared.scopes, view.token);
    release_runtime_unwind_image(&shared.scopes, old_token);
    if (epoch == 2)
      check(find_runtime_unwind_image(&shared.scopes, 0x502000, &view) ==
                ENOENT &&
            !view.token);
    shared.done.fetch_add(1);
  }
  lookup_only = false;
  return nullptr;
}

extern "C" int main() {
  checked_leases();
  Input main(ImageRole::MainPIE, 0x100000);
  Input late(ImageRole::SharedObject, 0x300000);
  Input missing(ImageRole::SharedObject, 0x500000, false);
  Input bad(ImageRole::SharedObject, 0x700000);
  bad.put(0xc00, 1, 2);
  const ImageRegistry::Image *images[] = {&main.record, &late.record,
                                          &missing.record, &bad.record};
  ImageRegistry::Snapshot registries[] = {
      {1, {images, 1}}, {2, {images, 2}}, {3, {images, 3}}, {4, images}};
  RuntimeScopes scopes(page);
  RuntimeScopes::Transaction startup(page);
  UnwindImageView view{};
  check(find_runtime_unwind_image(&scopes, 0x102000, &view) == EINVAL);
  check(!scopes.prepare_startup(registries[0], startup));
  check(!scopes.prepare_unwind(startup) && !scopes.publish(startup));
  check(!find_runtime_unwind_image(&scopes, 0x102000, &view));
  Address saved_token = view.token;
  Readers shared{scopes};
  pthread_t workers[2];
  for (auto &worker : workers)
    check(!pthread_create(&worker, nullptr, read, &shared));
  while (shared.ready.load() != 2)
    sched_yield();
  uint64_t token = 0;
  uint64_t late_handle = 0;
  for (unsigned epoch = 1; epoch <= 2; ++epoch) {
    RuntimeScopes::Transaction tx(page);
    const auto *before = scopes.snapshot();
    check(!scopes.prepare_open(registries[epoch], images[epoch], false, tx,
                               token));
    check(!scopes.can_publish(tx) && scopes.publish(tx) == -EINVAL);
    check(!scopes.prepare_unwind(tx) && scopes.can_publish(tx));
    check(scopes.snapshot() == before);
    check(find_runtime_unwind_image(&scopes, epoch == 1 ? 0x302000 : 0x502000,
                                    &view) == ENOENT);
    check(!scopes.publish(tx));
    if (epoch == 1)
      late_handle = token;
    shared.epoch.store(epoch);
    while (shared.done.load() != epoch * 2)
      sched_yield();
  }
  for (auto &worker : workers)
    check(!pthread_join(worker, nullptr));
  const auto *retained = scopes.snapshot()->unwind;
  RuntimeScopes::Transaction close(page);
  check(!scopes.prepare_close(token, close) && !scopes.prepare_unwind(close));
  check(!scopes.publish(close));
  check(scopes.snapshot()->unwind->registry == retained->registry);
  retained = scopes.snapshot()->unwind;
  release_runtime_unwind_image(&scopes, saved_token);
  check(!find_runtime_unwind_image(&scopes, 0x302000, &view));
  check(find_runtime_unwind_image(&scopes, 0x304000, &view) == ENOENT &&
        !view.token);
  RuntimeScopes::Transaction malformed(page);
  check(
      !scopes.prepare_open(registries[3], images[3], false, malformed, token));
  check(scopes.prepare_unwind(malformed) == -ENOEXEC);
  check(!scopes.can_publish(malformed) && !malformed.abort());
  check(scopes.snapshot()->unwind == retained);
  RuntimeScopes::Transaction close_late(page), retire(page);
  check(!scopes.prepare_close(late_handle, close_late));
  check(!scopes.prepare_unwind(close_late) && !scopes.publish(close_late));
  const ImageRegistry::Image *removed[] = {&late.record};
  check(!scopes.prepare_retire(removed, retire));
  check(!scopes.prepare_unwind(retire) && !scopes.publish(retire));
  check(find_runtime_unwind_image(&scopes, 0x302000, &view) == ENOENT);
  check(!scopes.readers_drained(&late.record)); // Copied cursors pin metadata.
  check(!scopes.collect_snapshots());
  release_runtime_unwind_image(&scopes, saved_token);
  // Small arena chunks force a mapping during frame metadata construction.
  RuntimeScopes failed(64);
  RuntimeScopes::Transaction failure(64);
  check(!failed.prepare_startup(registries[0], failure));
  fail_mapping = true;
  check(failed.prepare_unwind(failure) == -ENOMEM && !failed.snapshot());
  check(!failed.can_publish(failure) && failed.publish(failure) == -EINVAL);
  fail_mapping = false;
  check(!failure.abort());
  pid_t child = fork();
  check(child >= 0);
  if (!child) {
    release_runtime_unwind_image(&scopes, 1);
    _exit(1);
  }
  int status;
  check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
        WEXITSTATUS(status) == 127);
  std::puts("runtime unwind coherent publication, resident tokens and reader "
            "checks passed");
  return 0;
}
