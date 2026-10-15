//===-- Current-thread DTV refresh tests ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/dtv.h"
#include "loader/linux/syscall.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace LIBC_NAMESPACE::loader;
static constexpr Address page = 8192;
static thread_local Address tp, mask = 0x42;
static thread_local unsigned maps, unmaps, blocks, restores;
static thread_local bool fail_map, fail_unmap, fail_block, fail_restore;
static thread_local ThreadTLSAllocation *observed;
static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    std::fprintf(stderr, "DTV check failed at %d\n", line);
    std::abort();
  }
}

namespace LIBC_NAMESPACE_DECL {
namespace loader {
Address current_thread_pointer() { return tp; }
[[noreturn]] void fatal_startup() { _exit(127); }
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  if (n == SYS_rt_sigprocmask) {
    check(d == 8);
    if (a == SIG_BLOCK) {
      ++blocks;
      if (fail_block)
        return -EIO;
      check(c && *reinterpret_cast<uint64_t *>(b) == ~uint64_t(0));
      *reinterpret_cast<uint64_t *>(c) = mask;
      mask = ~Address(0);
    } else {
      check(a == SIG_SETMASK && mask == ~Address(0));
      ++restores;
      if (fail_restore)
        return -EIO;
      mask = *reinterpret_cast<uint64_t *>(b);
    }
    return 0;
  }
  check(mask == ~Address(0));
  if (n == SYS_mmap) {
    ++maps;
    check(a == 0 && b > 0 && !(b % page));
    if (fail_map)
      return -ENOMEM;
  } else {
    check(n == SYS_munmap);
    ++unmaps;
    // Publication precedes freeing, and the live descriptor matches TP.
    check(observed && Address(a) != *reinterpret_cast<Address *>(tp));
    check(observed->dtv_mapping_base == *reinterpret_cast<Address *>(tp));
    if (fail_unmap)
      return -EIO;
  }
  long result = ::syscall(n, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL

static Address allocate() {
  void *p = mmap(nullptr, page, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  check(p != MAP_FAILED);
  return reinterpret_cast<Address>(p);
}

struct Fixture {
  ThreadTLSAllocation allocation{};
  ImageRegistry::Image startup{}, late{}, added{}, extra{};
  const ImageRegistry::Image *images[4];
  ImageRegistry::Snapshot snapshot{};
  Address late_storage;
  DTVRefresh refresh;
  Fixture() {
    maps = unmaps = blocks = restores = 0;
    fail_map = fail_unmap = fail_block = fail_restore = false;
    mask = 0x42;
    allocation.tls_mapping_base = allocate();
    allocation.tls_mapping_size = page;
    tp = allocation.tp = allocation.tls_mapping_base;
    allocation.dtv_mapping_base = allocate();
    allocation.dtv_mapping_size = page;
    allocation.registry_cookie = 0x1234;
    late_storage = allocate();
    auto *dtv = reinterpret_cast<StartupDTV *>(allocation.dtv_mapping_base);
    *dtv = {4, 2, allocation.dtv_mapping_base + sizeof(StartupDTV)};
    auto *entries = reinterpret_cast<StartupDTVEntry *>(dtv->entries);
    entries[1] = {1, tp + 16, 0, 0};
    entries[3] = {2, late_storage + 8, late_storage, page};
    auto *header = reinterpret_cast<Address *>(tp);
    header[0] = allocation.dtv_mapping_base;
    header[1] = 0x5678;
    *reinterpret_cast<Address *>(tp + 16) = 42;
    *reinterpret_cast<Address *>(late_storage + 8) = 57;
    startup.startup = true;
    startup.tls.module_id = 1;
    startup.tls.generation = 1;
    startup.tls.tp_offset = 16;
    startup.tls.memory_size = 8;
    startup.tls.alignment = 8;
    late.tls.module_id = 3;
    late.tls.generation = 2;
    late.tls.memory_size = 8;
    late.tls.alignment = 32;
    late.tls.residue = 8;
    added.tls.module_id = 5;
    added.tls.generation = 3;
    images[0] = &startup;
    images[1] = &late;
    images[2] = &added;
    snapshot = {3, {images, 3}};
    observed = &allocation;
  }
  StartupDTV &dtv() {
    return *reinterpret_cast<StartupDTV *>(allocation.dtv_mapping_base);
  }
  StartupDTVEntry *entries() {
    return reinterpret_cast<StartupDTVEntry *>(dtv().entries);
  }
  long run() { return refresh.refresh(snapshot, allocation, page); }
  void unchanged(Address old) {
    check(allocation.dtv_mapping_base == old && dtv().generation == 2);
    check(*reinterpret_cast<Address *>(tp) == old && mask == 0x42);
  }
  void preserved() {
    check(allocation.tp == tp && allocation.tls_mapping_base == tp);
    check(entries()[1].image_base == tp + 16 && !entries()[1].allocation_owner);
    check(entries()[3].image_base == late_storage + 8 &&
          entries()[3].allocation_owner == late_storage &&
          entries()[3].allocation_extent == page);
    check(*reinterpret_cast<Address *>(tp + 16) == 42 &&
          *reinterpret_cast<Address *>(late_storage + 8) == 57);
    check(reinterpret_cast<Address *>(tp)[1] == 0x5678 &&
          allocation.registry_cookie == 0x1234 && mask == 0x42);
  }
  ~Fixture() {
    check(!refresh.cleanup_pending());
    check(!munmap(reinterpret_cast<void *>(allocation.dtv_mapping_base),
                  allocation.dtv_mapping_size));
    check(!munmap(reinterpret_cast<void *>(tp), page));
    check(!munmap(reinterpret_cast<void *>(late_storage), page));
    tp = 0;
    observed = nullptr;
  }
};

static void success() {
  Fixture f;
  Address old = f.allocation.dtv_mapping_base;
  check(!f.run() && f.dtv().capacity == 8 && f.dtv().generation == 3);
  check(f.allocation.dtv_mapping_base != old && maps == 1 && unmaps == 1);
  f.preserved();
  const unsigned vacant[] = {0, 2, 4, 5, 6, 7};
  for (unsigned i : vacant) {
    const auto &e = f.entries()[i];
    check(!e.generation && !e.image_base && !e.allocation_owner &&
          !e.allocation_extent);
  }
  old = f.allocation.dtv_mapping_base;
  f.extra.tls.module_id = 6;
  f.extra.tls.generation = 4;
  f.images[3] = &f.extra;
  f.snapshot = {4, {f.images, 4}};
  check(!f.run() && f.dtv().generation == 4 &&
        f.allocation.dtv_mapping_base == old);
  check(!f.run() && maps == 1 && unmaps == 1);
  f.preserved();
  check(blocks == restores);
}

extern "C" int main() {
  success();
  {
    Fixture f;
    Address old = f.allocation.dtv_mapping_base;
    fail_map = true;
    check(f.run() == ENOMEM);
    f.unchanged(old);
    fail_map = false;
    fail_block = true;
    check(f.run() == EIO && maps == 1);
    f.unchanged(old);
    fail_block = false;
    auto reject = [&](long expected = EINVAL) {
      check(f.run() == expected);
      f.unchanged(old);
    };
    f.entries()[3].generation = 1;
    reject();
    f.entries()[3].generation = 2;
    f.entries()[1].image_base += 8;
    reject();
    f.entries()[1].image_base -= 8;
    f.entries()[2] = {2, f.late_storage, f.late_storage, page};
    reject();
    f.entries()[2] = {};
    f.added.tls.module_id = ~Address(0);
    reject(EOVERFLOW);
    f.added.tls.module_id = ~Address(0) / 64;
    reject(EOVERFLOW);
    f.added.tls.module_id = 5;
    f.dtv().capacity = ~Address(0);
    check(f.run() == EINVAL && maps == 1 && mask == 0x42);
    f.dtv().capacity = 4;
    Address entries = f.dtv().entries;
    f.dtv().entries = old;
    reject();
    f.dtv().entries = entries;
    f.entries()[3].allocation_extent = 8;
    reject();
    f.entries()[3].allocation_extent = page;
    f.entries()[0] = {1, tp, 0, 0};
    reject();
    f.entries()[0] = {};
    f.snapshot.generation = 1;
    reject();
    f.snapshot.generation = 3;
    Address current = tp;
    tp = 0;
    check(f.run() == EINVAL);
    tp = current;
    check(!f.run());
    f.preserved();
  }
  {
    Fixture f;
    fail_unmap = true;
    check(f.run() == EIO && f.refresh.cleanup_pending());
    Address current = f.allocation.dtv_mapping_base;
    f.preserved();
    check(f.run() == EIO && maps == 1 &&
          f.allocation.dtv_mapping_base == current);
    fail_unmap = false;
    check(!f.run() && !f.refresh.cleanup_pending() && maps == 1);
    f.preserved();
  }
  {
    // First refresh from the startup-only generation must not allocate late
    // module images, even if the published snapshot contains those modules.
    Fixture f;
    f.dtv().generation = 1;
    f.entries()[3] = {};
    check(!f.run());
    check(!f.entries()[3].generation && !f.entries()[3].image_base);
    check(f.entries()[1].image_base == tp + 16);
  }
  pid_t child = fork();
  check(child >= 0);
  if (!child) {
    Fixture f;
    fail_restore = true;
    (void)f.run();
    _exit(1);
  }
  int status;
  check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
        WEXITSTATUS(status) == 127);
  pthread_t threads[4];
  for (auto &thread : threads)
    check(!pthread_create(
        &thread, nullptr,
        [](void *) -> void * {
          success();
          return nullptr;
        },
        nullptr));
  for (auto thread : threads)
    check(!pthread_join(thread, nullptr));
  std::puts("DTV growth, refresh, ownership, signal ordering and four "
            "independent threads passed");
  return 0;
}
