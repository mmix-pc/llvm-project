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
  check(LIBC_NAMESPACE::mmix::adopt_startup_tls_resolver(
      {&context, resolve_startup_tls}));
  Address original = current_thread_pointer();
  for (unsigned i = 0; i < 2; ++i) {
    check(prepare_thread_tls(&context, nullptr, &blocks[i]) == 0);
    set_tp(blocks[i].tp);
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
    TLSIndex gd{1, 8};
    check(*static_cast<Address *>(__tls_get_addr(&gd)) == i + 10);
    check(current_thread_pointer() == blocks[i].tp);
    set_tp(original);
    check(release_thread_tls(&context, &blocks[i]) == 0);
  }
  check(current_thread_pointer() == original);
  static const char message[] = "MMIX_TLS PASS\n";
  raw_syscall(SYS_write, 1, reinterpret_cast<long>(message),
              sizeof(message) - 1);
  for (;;)
    raw_syscall(SYS_sched_yield);
}
