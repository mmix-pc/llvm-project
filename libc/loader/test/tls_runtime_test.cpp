//===-- Startup TLS relocation and allocation tests -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/target.h"
#include "loader/linux/tls_allocation.h"
#include <errno.h>
#include <initializer_list>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>

using namespace loader_test;
namespace cpp = LIBC_NAMESPACE::cpp;

static void check(bool condition, int line = __builtin_LINE()) {
  if (!condition) {
    fprintf(stderr, "TLS runtime check failed at line %d\n", line);
    abort();
  }
}

static unsigned maps, unmaps, fail_map, fail_unmap, live;
namespace LIBC_NAMESPACE_DECL {
namespace loader {
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  if (n == SYS_mmap) {
    if (++maps == fail_map)
      return -ENOMEM;
    void *p = mmap(reinterpret_cast<void *>(a), b, c, d, e, f);
    check(p != MAP_FAILED);
    ++live;
    return reinterpret_cast<long>(p);
  }
  check(n == SYS_munmap);
  if (++unmaps == fail_unmap)
    return -EIO;
  check(munmap(reinterpret_cast<void *>(a), b) == 0 && live);
  --live;
  return 0;
}
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL

// Low native addresses let the MMIX address checks and template pointers agree.
alignas(8192) static unsigned char memory[2][0x6000];
static Address octa(const unsigned char *p) {
  Address value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value = (value << 8) | p[i];
  return value;
}

struct Scope {
  DynamicFixture files[2] = {DynamicFixture(ImageRole::MainPIE),
                             DynamicFixture()};
  ElfImage images[2];
  DynamicInfo metadata[2];
  MappedRegion regions[2];
  WritableRegion writes[2];
  cpp::span<const WritableRegion> writable[2];
  SymbolObject objects[2];
  StartupTLSModule modules[2];
  StartupTLSSet layout;
  RelocatedTLS ready;
  Scope() {
    for (unsigned i = 0; i < 2; ++i) {
      auto &f = files[i];
      f.segment(7, PT_TLS, PF_R, 0x4a08, 0x4a08, 16, 64, i ? 64 : 32);
      f.put(0x81c, 1, (STB_GLOBAL << 4) | STT_TLS);
      f.put(0x820, 8, 8);
      f.put(0x828, 8, 8);
      f.put(0xb20, 8, (Address(1) << 32) | 45);
      f.put(0xb28, 8, Address(-8));
      // An ordinary pointer initializer inside the TLS template.
      f.set(DT_RELASZ, 72);
      f.put(0xb30, 8, 0x4a08);
      f.put(0xb38, 8, 41);
      f.put(0xb40, 8, 0x2000);
      for (unsigned j = 8; j < 16; ++j)
        f.bytes[0x4a08 + j] = 0xa5;
    }
  }
  void open() {
    for (unsigned i = 0; i < 2; ++i) {
      for (unsigned j = 0; j < sizeof(memory[i]); ++j)
        memory[i][j] = files[i].bytes[j];
      regions[i] = {0, {memory[i], sizeof(memory[i])}};
      check(ElfImage::open_mapped({&regions[i], 1}, 0, 64,
                                  i ? ImageRole::SharedObject
                                    : ImageRole::MainPIE,
                                  reinterpret_cast<Address>(memory[i]), page,
                                  images[i]) == ElfError::None);
      check(DynamicInfo::open(images[i], metadata[i]) == DynamicError::None);
      objects[i] = {&images[i], &metadata[i]};
      writes[i] = {0x4000, {memory[i] + 0x4000, 0x2000}};
      writable[i] = {&writes[i], 1};
    }
    const ElfImage *views[] = {&images[0], &images[1]};
    check(plan_startup_tls(views, modules, layout) == TLSLayoutError::None);
  }
  StartupRelocationError relocate() {
    return relocate_startup_tls(objects, writable, layout, ready).error;
  }
  void untouched() {
    check(!ready.modules());
    for (unsigned i = 0; i < 2; ++i)
      check(octa(memory[i] + 0x4800) == 0 && octa(memory[i] + 0x4808) == 0 &&
            octa(memory[i] + 0x4a08) == 0);
  }
};

static void allocations(Scope &scope) {
  static_assert(sizeof(ThreadTLSAllocation) == 56);
  static_assert(sizeof(StartupDTV) == 32);
  static_assert(sizeof(StartupDTVEntry) == 32);
  TLSAllocationContext context{&scope.ready, page};
  ThreadTLSAllocation a{}, b{};
  RelocatedTLS not_ready;
  TLSAllocationContext premature{&not_ready, page};
  check(prepare_thread_tls(&premature, nullptr, &a) == EINVAL && !maps);
  TLSAllocationContext bad_page{&scope.ready, 0};
  check(prepare_thread_tls(&bad_page, nullptr, &a) == EINVAL && !maps);
  for (unsigned fail : {1, 2}) {
    maps = unmaps = 0;
    fail_map = fail;
    check(prepare_thread_tls(&context, nullptr, &a) == ENOMEM);
    check(!a.tp && !a.tls_mapping_size && !a.dtv_mapping_size && !live);
  }
  maps = unmaps = 0;
  fail_map = 2;
  fail_unmap = 1;
  check(prepare_thread_tls(&context, nullptr, &a) == ENOMEM);
  check(!a.tp && !a.registry_cookie && a.tls_mapping_size && live == 1);
  check(prepare_thread_tls(&context, nullptr, &a) == EBUSY);
  fail_map = fail_unmap = 0;
  check(release_thread_tls(&context, &a) == 0 && !live);
  check(prepare_thread_tls(&context, &scope, &a) == 0);
  check(prepare_thread_tls(&context, nullptr, &b) == 0 && live == 4);
  check(a.tp != b.tp && a.dtv_mapping_base != b.dtv_mapping_base);
  check(a.tp % scope.layout.tp_alignment == 0 && a.tp >= a.tls_mapping_base &&
        a.tp + scope.layout.tls_extent <=
            a.tls_mapping_base + a.tls_mapping_size);
  auto *header = reinterpret_cast<uint64_t *>(a.tp);
  check(header[1] == reinterpret_cast<uint64_t>(&scope));
  auto *dtv = reinterpret_cast<StartupDTV *>(header[0]);
  check(header[0] == a.dtv_mapping_base && dtv->capacity == 3 &&
        dtv->generation == 1);
  auto *entries = reinterpret_cast<StartupDTVEntry *>(dtv->entries);
  check(!entries[0].generation && !entries[0].image_base);
  for (unsigned i = 0; i < 2; ++i) {
    const auto &m = scope.modules[i];
    check(entries[i + 1].generation == 1 &&
          entries[i + 1].image_base == a.tp + m.tp_offset &&
          !entries[i + 1].allocation_owner &&
          !entries[i + 1].allocation_extent);
    const auto *p =
        reinterpret_cast<const unsigned char *>(entries[i + 1].image_base);
    check(octa(p) == reinterpret_cast<Address>(memory[i]) + 0x2000);
    for (unsigned j = 8; j < 64; ++j)
      check(p[j] == (j < 16 ? 0xa5 : 0));
  }
  *reinterpret_cast<unsigned char *>(a.tp + scope.modules[0].tp_offset) = 0xff;
  check(*reinterpret_cast<unsigned char *>(b.tp + scope.modules[0].tp_offset) !=
        0xff);
  check(release_thread_tls(&context, &b) == 0);
  // Either independently owned mapping may fail release without losing it.
  for (unsigned which : {1, 2}) {
    unmaps = 0;
    fail_unmap = which;
    check(release_thread_tls(&context, &a) == EIO && live == 1);
    check(!a.tp && !a.registry_cookie);
    check(bool(a.dtv_mapping_size) == (which == 1));
    check(bool(a.tls_mapping_size) == (which == 2));
    fail_unmap = 0;
    check(release_thread_tls(&context, &a) == 0 && !live);
    if (which == 1)
      check(prepare_thread_tls(&context, nullptr, &a) == 0);
  }
  check(release_thread_tls(&context, &a) == 0);
}

int main() {
  Scope scope;
  scope.open();
  check(relocate_startup(scope.objects, scope.writable).error ==
        StartupRelocationError::Image);
  check(scope.relocate() == StartupRelocationError::None);
  check(scope.modules[0].tp_offset == 40 && scope.modules[1].tp_offset == 136);
  check(octa(memory[1] + 0x4808) == 0); // Preempted main symbol offset 8 - 8.
  allocations(scope);
  for (unsigned type : {44, 46}) {
    Scope input;
    input.files[1].put(0xb20, 8, (Address(1) << 32) | type);
    input.files[1].put(0xb28, 8, 0);
    input.open();
    check(input.relocate() == StartupRelocationError::None);
    check(octa(memory[1] + 0x4808) == (type == 44 ? 1 : 48));
    input.files[1].put(0x81d, 1, STV_PROTECTED);
    input.open();
    check(input.relocate() == StartupRelocationError::None);
    check(octa(memory[1] + 0x4808) == (type == 44 ? 2 : 144));
    input.files[1].put(0xb20, 8, type);
    input.files[1].put(0xb28, 8, type == 44 ? 0 : 8);
    input.open();
    check(input.relocate() == StartupRelocationError::None);
    check(octa(memory[1] + 0x4808) == (type == 44 ? 2 : 144));
  }
  for (unsigned mutation = 0; mutation < 8; ++mutation) {
    Scope bad;
    if (mutation < 2) {
      for (auto &f : bad.files) {
        f.put(0x81e, 2, SHN_UNDEF);
        f.put(0x820, 8, 0);
        if (mutation == 1)
          f.put(0x81c, 1, (STB_WEAK << 4) | STT_TLS);
      }
    }
    if (mutation == 2)
      bad.files[1].put(0xb28, 8, Address(-9));
    if (mutation == 3)
      bad.files[1].put(0xb28, 8, Address(1) << 62);
    if (mutation == 7) {
      // A same-named ordinary definition must not satisfy a TLS reference.
      bad.files[0].put(0x81c, 1, (STB_GLOBAL << 4) | STT_FUNC);
      bad.files[0].put(0x820, 8, 0x2000);
      bad.files[0].put(0xb20, 8, (Address(1) << 32) | 42);
    }
    bad.open();
    if (mutation == 4)
      ++bad.modules[1].tp_offset;
    if (mutation == 5)
      bad.writes[1].bytes = bad.writes[0].bytes;
    if (mutation == 6)
      memory[1][0xb1f] |= 1;
    check(bad.relocate() != StartupRelocationError::None);
    bad.untouched();
  }
  Address value;
  check(!tls_relocation_value(46, 1, ~Address(0), 8, 8, 0, value));
  check(!tls_relocation_value(45, 1, 16, ~Address(0), ~Address(0), 1, value));
  // Every small alignment residue plus an alignment exceeding a page.
  for (Address align = 1; align <= 64; align *= 2) {
    for (Address residue = 0; residue < align; ++residue) {
      Scope input;
      input.files[1].segment(7, PT_TLS, PF_R, 0x4a00 + residue,
                             0x4a00 + residue, 0, 64, align);
      input.open();
      check(input.relocate() == StartupRelocationError::None);
      TLSAllocationContext context{&input.ready, page};
      ThreadTLSAllocation a{};
      check(prepare_thread_tls(&context, nullptr, &a) == 0);
      check((a.tp + input.modules[1].tp_offset) % align == residue);
      check(release_thread_tls(&context, &a) == 0);
    }
  }
  Scope aligned;
  aligned.files[1].segment(7, PT_TLS, PF_R, 0x4a08, 0x4a08, 16, 64, 16384);
  aligned.open();
  check(aligned.relocate() == StartupRelocationError::None);
  TLSAllocationContext context{&aligned.ready, page};
  ThreadTLSAllocation a{};
  check(prepare_thread_tls(&context, nullptr, &a) == 0 && a.tp % 16384 == 0);
  check(release_thread_tls(&context, &a) == 0 && !live);
  Scope empty;
  empty.files[0].put(ph(7), 4, PT_NULL);
  empty.files[0].put(0x81c, 1, (STB_GLOBAL << 4) | STT_FUNC);
  empty.files[0].put(0x820, 8, 0x2000);
  empty.files[0].put(0xb20, 8, (Address(1) << 32) | 42);
  empty.files[1].segment(7, PT_TLS, PF_R, 0x4a08, 0x4a08, 0, 0, 64);
  empty.files[1].put(0x81d, 1, STV_PROTECTED);
  empty.files[1].put(0x820, 8, 0);
  empty.files[1].put(0x828, 8, 0);
  empty.files[1].put(0xb20, 8, 44);
  empty.files[1].put(0xb28, 8, 0);
  empty.open();
  check(empty.relocate() == StartupRelocationError::None);
  context.relocated = &empty.ready;
  check(prepare_thread_tls(&context, nullptr, &a) == 0);
  check(empty.modules[0].module_id == 1 && empty.modules[1].module_id == 2 &&
        !empty.modules[0].memory_size && !empty.modules[1].memory_size);
  check(release_thread_tls(&context, &a) == 0 && !live);
  return 0;
}
