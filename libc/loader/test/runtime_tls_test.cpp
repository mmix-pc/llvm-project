//===-- Late TLS resolver tests -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/runtime_tls.h"
#include "loader/linux/syscall.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace loader_test;
alignas(8192) static unsigned char memory[3][0x6000];
static Address tp, mask = 0x42;
static unsigned maps, fail_map, restores, fail_restore;
static bool fail_block;
static void check(bool ok, int line = __builtin_LINE()) {
  if (!ok) {
    std::fprintf(stderr, "runtime TLS check failed at %d\n", line);
    std::abort();
  }
}
namespace LIBC_NAMESPACE_DECL {
namespace loader {
Address current_thread_pointer() { return tp; }
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  if (n == SYS_rt_sigprocmask) {
    check(d == 8);
    if (a == SIG_BLOCK) {
      if (fail_block)
        return -EIO;
      *reinterpret_cast<uint64_t *>(c) = mask;
      mask = ~Address(0);
    } else {
      check(a == SIG_SETMASK && mask == ~Address(0));
      if (++restores == fail_restore)
        return -EIO;
      mask = *reinterpret_cast<uint64_t *>(b);
    }
    return 0;
  }
  if (n == SYS_mmap || n == SYS_munmap)
    check(mask == ~Address(0));
  if (n == SYS_mmap && ++maps == fail_map)
    return -ENOMEM;
  long result = ::syscall(n, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL

struct RuntimeFixture {
  DynamicFixture files[3] = {DynamicFixture(ImageRole::MainPIE),
                             DynamicFixture(), DynamicFixture()};
  MappedRegion regions[3];
  ImageRegistry::Image images[3]{};
  const ImageRegistry::Image *records[3];
  ImageRegistry::Snapshot snapshot;
  ThreadTLSAllocation allocation{};
  DTVRefresh refresh;
  RuntimeTLSContext context;
  RuntimeFixture()
      : snapshot{2, {records, 3}},
        context{&snapshot, &allocation, &refresh, page} {
    maps = restores = fail_map = fail_restore = 0;
    fail_block = false;
    mask = 0x42;
    for (unsigned i = 0; i < 3; ++i) {
      files[i].segment(7, PT_TLS, PF_R, 0x4a08, 0x4a08, i == 2 ? 0 : 16, 64,
                       i == 1 ? 32768 : 8);
      // Represents a pointer initializer after relocation, not executable code.
      files[i].put(0x4a08, 8, reinterpret_cast<Address>(memory[i]) + 0x2000);
      files[i].bytes[0x4a10] = 0xa5;
      std::memcpy(memory[i], files[i].bytes, sizeof(memory[i]));
      regions[i] = {0, {memory[i], sizeof(memory[i])}};
      auto &image = images[i];
      check(ElfImage::open_mapped({regions + i, 1}, 0, 64,
                                  i ? ImageRole::SharedObject
                                    : ImageRole::MainPIE,
                                  reinterpret_cast<Address>(memory[i]), page,
                                  image.elf) == ElfError::None);
      Address cursor = i ? 0 : 16;
      check(place_tls_module(image.elf, cursor, image.tls));
      image.startup = !i;
      image.tls.module_id = i * 2 + 1;
      image.tls.generation = i ? 2 : 1;
      if (i)
        image.tls.tp_offset = 0;
      records[i] = &image;
    }
    void *block = mmap(nullptr, page, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(block != MAP_FAILED);
    allocation.tp = allocation.tls_mapping_base = tp =
        reinterpret_cast<Address>(block);
    allocation.tls_mapping_size = page;
    block = mmap(nullptr, page, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(block != MAP_FAILED);
    allocation.dtv_mapping_base = reinterpret_cast<Address>(block);
    allocation.dtv_mapping_size = page;
    allocation.registry_cookie = 1234;
    auto *dtv = static_cast<StartupDTV *>(block);
    *dtv = {2, 1, allocation.dtv_mapping_base + sizeof(StartupDTV)};
    auto *entries = reinterpret_cast<StartupDTVEntry *>(dtv->entries);
    entries[1] = {1, tp + images[0].tls.tp_offset, 0, 0};
    reinterpret_cast<Address *>(tp)[0] = allocation.dtv_mapping_base;
    reinterpret_cast<Address *>(tp)[1] = 5678;
    *reinterpret_cast<Address *>(entries[1].image_base) = 42;
  }
  StartupDTVEntry *entries() {
    return reinterpret_cast<StartupDTVEntry *>(
        reinterpret_cast<StartupDTV *>(allocation.dtv_mapping_base)->entries);
  }
  void *get(Address id, Address offset = 0) {
    TLSIndex index{id, offset};
    return resolve_runtime_tls(&context, &index);
  }
  ~RuntimeFixture() {
    auto *dtv = reinterpret_cast<StartupDTV *>(allocation.dtv_mapping_base);
    for (Address i = 0; i < dtv->capacity; ++i)
      if (entries()[i].allocation_extent)
        check(!munmap(reinterpret_cast<void *>(entries()[i].allocation_owner),
                      entries()[i].allocation_extent));
    check(!refresh.cleanup_pending());
    check(!munmap(reinterpret_cast<void *>(allocation.dtv_mapping_base),
                  allocation.dtv_mapping_size));
    check(!munmap(reinterpret_cast<void *>(allocation.tls_mapping_base), page));
  }
};

extern "C" int main() {
  {
    RuntimeFixture f;
    Address original_tp = tp;
    auto *gd = static_cast<unsigned char *>(f.get(3, 8));
    auto *ld = static_cast<unsigned char *>(f.get(3));
    check(gd == ld + 8 && *gd == 0xa5 && maps == 2);
    check((reinterpret_cast<Address>(ld) & 32767) == (0x4a08 & 32767));
    check(!std::memcmp(ld, memory[1] + 0x4a08, 16));
    for (unsigned i = 16; i < 64; ++i)
      check(ld[i] == 0);
    gd[0] = 73;
    check(f.get(3, 64) == ld + 64 &&
          *static_cast<unsigned char *>(f.get(3, 8)) == 73);
    check(maps == 2 && memory[1][0x4a10] == 0xa5);
    auto *bss = static_cast<unsigned char *>(f.get(5));
    for (unsigned i = 0; i < 64; ++i)
      check(!bss[i]);
    check(maps == 3 && bss != ld);
    check(f.get(1) == reinterpret_cast<void *>(tp + f.images[0].tls.tp_offset));
    check(*static_cast<Address *>(f.get(1)) == 42 && maps == 3);
    check(tp == original_tp && reinterpret_cast<Address *>(tp)[1] == 5678 &&
          mask == 0x42);
    check(f.entries()[3].allocation_owner && f.entries()[3].allocation_extent);
  }
  {
    RuntimeFixture f;
    f.get(3);
    f.records[1] = &f.images[2];
    f.snapshot.images = {f.records, 2};
    // No access to retired template metadata is needed during owner cleanup.
    f.images[1].tls.template_address = 1;
    check(*static_cast<Address *>(f.get(1)) == 42);
    check(!f.entries()[3].generation && !f.entries()[3].allocation_extent);
    ++f.snapshot.generation;
    Address cursor = 0;
    check(place_tls_module(f.images[1].elf, cursor, f.images[1].tls));
    f.images[1].tls.module_id = 7;
    f.images[1].tls.generation = f.snapshot.generation;
    f.records[2] = &f.images[1];
    f.snapshot.images = {f.records, 3};
    check(*static_cast<unsigned char *>(f.get(7, 8)) == 0xa5);
    check(!f.entries()[3].generation);
  }
  // Fatal cases execute the production diagnostic/exit path in a child.
  for (unsigned scenario = 0; scenario < 12; ++scenario) {
    int pipefd[2];
    check(!pipe(pipefd));
    pid_t child = fork();
    check(child >= 0);
    if (!child) {
      close(pipefd[0]);
      check(dup2(pipefd[1], 2) == 2);
      close(pipefd[1]);
      RuntimeFixture f;
      switch (scenario) {
      case 0:
        fail_map = 1;
        break; // DTV growth.
      case 1:
        fail_map = 2;
        break; // Module storage.
      case 2:
        f.get(2);
        break; // An unassigned module ID.
      case 3:
        f.get(3, 65);
        break;
      case 4:
        f.images[1].tls.template_address = 1;
        break;
      case 5:
        f.images[1].tls.generation = 3;
        break;
      case 6:
        f.get(3);
        f.entries()[3].generation = 1;
        break;
      case 7:
        tp = 0;
        break;
      case 8:
        fail_block = true;
        break;
      case 9:
        fail_restore = 2;
        break; // Outer mask restore.
      case 10:
        resolve_runtime_tls(nullptr, nullptr);
        break;
      case 11:
        f.get(3);
        f.records[1] = &f.images[2];
        f.snapshot.images = {f.records, 2};
        f.images[1].tls.template_address = 1;
        break; // Retired ID fails before reading obsolete template bytes.
      }
      f.get(3);
      _exit(1);
    }
    close(pipefd[1]);
    char message[128]{};
    ssize_t size = read(pipefd[0], message, sizeof(message) - 1);
    close(pipefd[0]);
    int status;
    check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 127);
    check(size > 0 && std::strstr(message, "LLVM loader: dynamic TLS failure"));
  }
  std::puts("runtime TLS allocation, GD/LD identity, BSS, alignment and fatal "
            "cases passed");
  return 0;
}
