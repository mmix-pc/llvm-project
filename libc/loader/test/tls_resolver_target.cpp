//===-- MMIX startup TLS resolver execution harness -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/syscall.h"
#include "loader/linux/tls_resolver.h"
#ifdef TEST_RUNTIME_TLS
#include "loader/linux/runtime_tls.h"
#endif
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#include <sys/syscall.h>

using namespace loader_test;
namespace cpp = LIBC_NAMESPACE::cpp;
// O0 fixture aggregate initialization needs memset; production resolver objects
// must remain independent of this harness-only support.
extern "C" void *memset(void *p, int value, __SIZE_TYPE__ size) {
  auto *bytes = static_cast<volatile unsigned char *>(p);
  for (__SIZE_TYPE__ i = 0; i < size; ++i)
    bytes[i] = static_cast<unsigned char>(value);
  return p;
}
static void check(bool value) {
  if (!value) {
    static const char message[] = "MMIX_TLS FAIL resolver\n";
    raw_syscall(SYS_write, 1, reinterpret_cast<long>(message),
                sizeof(message) - 1);
    __builtin_trap();
  }
}
static void set_tp(Address tp) {
  // The harness changes TP only while executing entirely TLS-free code.
  asm volatile("OR r230, %0, 0" : : "r"(tp) : "memory");
}
extern "C" void supervise() {
  alignas(8192) DynamicFixture fixture(ImageRole::MainPIE);
  fixture.segment(7, PT_TLS, PF_R, 0x4a00, 0x4a00, 8, 32, 64);
  ElfImage image;
  MappedRegion region{0, {fixture.bytes, sizeof(fixture.bytes)}};
  check(ElfImage::open_mapped({&region, 1}, 0, 64, ImageRole::MainPIE,
                              reinterpret_cast<Address>(fixture.bytes), page,
                              image) == ElfError::None);
  DynamicInfo metadata;
  check(DynamicInfo::open(image, metadata) == DynamicError::None);
  SymbolObject object{&image, &metadata};
  WritableRegion writable{0x4000, {fixture.bytes + 0x4000, 0x2000}};
  cpp::span<const WritableRegion> writes{&writable, 1};
  const ElfImage *images[] = {&image};
  StartupTLSModule module;
  StartupTLSSet set;
  check(plan_startup_tls(images, {&module, 1}, set) == TLSLayoutError::None);
  RelocatedTLS ready;
  check(relocate_startup_tls({&object, 1}, {&writes, 1}, set, ready).error ==
        StartupRelocationError::None);
  TLSAllocationContext context{&ready, page};
  ThreadTLSAllocation blocks[2]{};
#ifdef TEST_RUNTIME_TLS
  alignas(8192) DynamicFixture late;
  late.segment(7, PT_TLS, PF_R, 0x4a08, 0x4a08, 16, 64, 32768);
  // A pointer initializer already relocated to this image's code address.
  late.put(0x4a08, 8, reinterpret_cast<Address>(late.bytes) + 0x2000);
  late.bytes[0x4a10] = 0xa5;
  MappedRegion late_region{0, {late.bytes, sizeof(late.bytes)}};
  ImageRegistry::Image startup_image{}, late_image{};
  startup_image.elf = image;
  startup_image.tls = module;
  startup_image.startup = true;
  check(ElfImage::open_mapped({&late_region, 1}, 0, 64, ImageRole::SharedObject,
                              reinterpret_cast<Address>(late.bytes), page,
                              late_image.elf) == ElfError::None);
  Address cursor = 0;
  check(place_tls_module(late_image.elf, cursor, late_image.tls));
  late_image.tls.module_id = 3;
  late_image.tls.generation = 2;
  late_image.tls.tp_offset = 0;
  const ImageRegistry::Image *records[] = {&startup_image, &late_image};
  ImageRegistry::Snapshot snapshot{2, records};
  DTVRefresh refresh[2];
  RuntimeTLSContext runtime{&snapshot, nullptr, nullptr, page};
  check(LIBC_NAMESPACE::mmix::adopt_startup_tls_resolver(
      {&runtime, resolve_runtime_tls}));
#else
  check(LIBC_NAMESPACE::mmix::adopt_startup_tls_resolver(
      {&context, resolve_startup_tls}));
#endif
  Address original = current_thread_pointer();
  for (unsigned i = 0; i < 2; ++i) {
    check(prepare_thread_tls(&context, nullptr, &blocks[i]) == 0);
    set_tp(blocks[i].tp);
#ifdef TEST_RUNTIME_TLS
    runtime.allocation = &blocks[i];
    runtime.refresh = &refresh[i];
    TLSIndex late_gd{3, 8}, late_ld{3, 0};
    auto *ld_address = static_cast<unsigned char *>(__llvm_libc_tls_get_addr(&late_ld));
    check(static_cast<unsigned char *>(__tls_get_addr(&late_gd)) == ld_address + 8);
    check(ld_address[8] == 0xa5);
    check((reinterpret_cast<Address>(ld_address) & 32767) == (0x4a08 & 32767));
    for (unsigned j = 0; j < 16; ++j)
      check(ld_address[j] == late.bytes[0x4a08 + j]);
    for (unsigned j = 16; j < 64; ++j)
      check(ld_address[j] == 0);
    ld_address[8] = static_cast<unsigned char>(i + 20);
    check(static_cast<unsigned char *>(__tls_get_addr(&late_gd))[0] == i + 20);
#endif
    TLSIndex gd{1, 8}, ld{1, 0};
    Address ie = blocks[i].tp + module.tp_offset + 8;
    check(reinterpret_cast<Address>(__tls_get_addr(&gd)) == ie);
    check(reinterpret_cast<Address>(__llvm_libc_tls_get_addr(&ld)) + 8 == ie);
    check(current_thread_pointer() == blocks[i].tp);
    *reinterpret_cast<Address *>(ie) = i + 10;
    set_tp(original);
  }
  check(blocks[0].tp != blocks[1].tp);
  for (unsigned i = 0; i < 2; ++i) {
    set_tp(blocks[i].tp);
#ifdef TEST_RUNTIME_TLS
    runtime.allocation = &blocks[i];
    runtime.refresh = &refresh[i];
    TLSIndex late_gd{3, 8};
    check(*static_cast<unsigned char *>(__tls_get_addr(&late_gd)) == i + 20);
#endif
    TLSIndex gd{1, 8};
    check(*static_cast<Address *>(__tls_get_addr(&gd)) == i + 10);
    check(current_thread_pointer() == blocks[i].tp);
    set_tp(original);
#ifdef TEST_RUNTIME_TLS
    auto *dtv = reinterpret_cast<StartupDTV *>(blocks[i].dtv_mapping_base);
    auto &entry = reinterpret_cast<StartupDTVEntry *>(dtv->entries)[3];
    check(!refresh[i].cleanup_pending());
    check(!raw_syscall(SYS_munmap, entry.allocation_owner, entry.allocation_extent));
#endif
    check(release_thread_tls(&context, &blocks[i]) == 0);
  }
  check(current_thread_pointer() == original);
  static const char message[] = "MMIX_TLS PASS\n";
  raw_syscall(SYS_write, 1, reinterpret_cast<long>(message),
              sizeof(message) - 1);
  for (;;)
    raw_syscall(SYS_sched_yield);
}
