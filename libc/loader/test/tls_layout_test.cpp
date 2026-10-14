//===-- Startup TLS layout tests ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "elf_fixture.h"
#include "loader/linux/tls_layout.h"
#include <initializer_list>
#include <stdio.h>
#include <stdlib.h>

using namespace loader_test;

static void check(bool condition, int line = __builtin_LINE()) {
  if (!condition) {
    fprintf(stderr, "TLS layout check failed at line %d\n", line);
    abort();
  }
}

struct Input {
  Fixture file;
  ElfImage image;
  ImageRole role;
  explicit Input(ImageRole r = ImageRole::SharedObject) : file(r), role(r) {
    open();
  }
  void open() {
    check(ElfImage::open_file(file.view(), role,
                              role == ImageRole::MainExecutable ? 0 : bias,
                              page, image) == ElfError::None);
  }
  void tls(Address memory, Address alignment, Address residue,
           Address initialized = 0) {
    Address offset = 0x4800 + residue;
    file.segment(7, PT_TLS, PF_R, offset,
                 offset + (role == ImageRole::MainExecutable ? base : 0),
                 initialized, memory, alignment);
    open();
  }
};

extern "C" int main() {
  static_assert(sizeof(StartupTLSModule) == 64);
  static_assert(sizeof(StartupTLSSet) == 40);
  Input main(ImageRole::MainPIE), second, third;
  main.tls(24, 8, 0, 8);
  second.tls(16, 64, 8, 8);
  third.tls(0, 16, 0);
  const ElfImage *images[] = {&main.image, &second.image, &third.image};
  StartupTLSModule modules[3];
  StartupTLSSet set;
  check(plan_startup_tls(images, modules, set) == TLSLayoutError::None);
  check(set.modules == modules && set.module_count == 3 && set.generation == 1);
  check(set.tp_alignment == 64 && set.tls_extent == 96);
  check(modules[0].tp_offset == 16 && modules[1].tp_offset == 72 &&
        modules[2].tp_offset == 96);
  check(modules[0].file_size == 8 && modules[0].memory_size == 24);
  check(modules[1].template_address == bias + 0x4808);
  for (unsigned i = 0; i < 3; ++i)
    check(modules[i].module_id == i + 1 && modules[i].generation == 1);

  // Main LE and IE share the first-module formula in either main image role.
  for (auto role : {ImageRole::MainPIE, ImageRole::MainExecutable}) {
    Input owner(role);
    const ElfImage *one[] = {&owner.image};
    check(plan_startup_tls(one, modules, set) == TLSLayoutError::None);
    check(modules[0].module_id == 1 && modules[0].memory_size == 0 &&
          modules[0].template_address == 0 && modules[0].alignment == 1 &&
          modules[0].residue == 0 && modules[0].tp_offset == 16);
    check(set.tp_alignment == 8 && set.tls_extent == 16);
    owner.tls(32, 128, 8, 8);
    check(plan_startup_tls(one, modules, set) == TLSLayoutError::None);
    check(modules[0].tp_offset == 136 && set.tls_extent == 168 &&
          set.tp_alignment == 128);
    owner.tls(0, 0, 0);
    check(plan_startup_tls(one, modules, set) == TLSLayoutError::None);
    check(modules[0].alignment == 1 && set.tls_extent == 16);
  }
  // Exhaustively compare small residues against a simple incremental oracle.
  for (Address alignment = 1; alignment <= 128; alignment *= 2)
    for (Address residue = 0; residue < alignment; ++residue)
      for (Address cursor : {Address(16), Address(40), Address(88)}) {
        main.tls(cursor - 16, 1, 0);
        second.tls(13, alignment, residue);
        check(plan_startup_tls({images, 2}, modules, set) ==
              TLSLayoutError::None);
        Address expected = cursor;
        while (expected % alignment != residue)
          ++expected;
        check(modules[1].tp_offset == expected &&
              set.tls_extent == expected + 13);
        check(set.tp_alignment == (alignment < 8 ? 8 : alignment));
      }

  Input absent(ImageRole::MainPIE), no_tls;
  const ElfImage *mixed[] = {&absent.image, &no_tls.image, &third.image};
  check(plan_startup_tls(mixed, modules, set) == TLSLayoutError::None);
  check(set.module_count == 3 && modules[1].module_id == 2 &&
        modules[2].module_id == 3 && set.tls_extent == 16);

  // Invalid scopes and late arithmetic failures never partially write storage.
  auto failed = [&](auto scope, TLSLayoutError error) {
    modules[0].module_id = 99;
    check(plan_startup_tls(scope, modules, set) == error);
    check(!set.modules && !set.module_count && !set.generation &&
          !set.tp_alignment && !set.tls_extent);
    check(modules[0].module_id == 99);
  };
  using Scope = LIBC_NAMESPACE::cpp::span<const ElfImage *const>;
  failed(Scope{}, TLSLayoutError::Scope);
  const ElfImage *invalid[] = {&second.image};
  failed(Scope{invalid, 1}, TLSLayoutError::Scope);
  invalid[0] = nullptr;
  failed(Scope{invalid, 1}, TLSLayoutError::Scope);
  ElfImage unopened;
  invalid[0] = &unopened;
  failed(Scope{invalid, 1}, TLSLayoutError::Scope);
  const ElfImage *duplicate[] = {&main.image, &second.image, &second.image};
  failed(Scope{duplicate, 3}, TLSLayoutError::Scope);
  const ElfImage *two_mains[] = {&main.image, &absent.image};
  failed(Scope{two_mains, 2}, TLSLayoutError::Scope);
  check(plan_startup_tls(images, {modules, 2}, set) == TLSLayoutError::Storage);
  check(!set.modules && modules[0].module_id == 99);

  Input huge[5];
  for (auto &input : huge)
    input.tls(1, Address(1) << 62, 0);
  const ElfImage *overflow[] = {&absent.image,  &huge[0].image, &huge[1].image,
                                &huge[2].image, &huge[3].image, &huge[4].image};
  StartupTLSModule large_storage[6];
  large_storage[0].module_id = 99;
  check(plan_startup_tls(overflow, large_storage, set) ==
        TLSLayoutError::Overflow);
  check(!set.modules && large_storage[0].module_id == 99);
  // Four large modules fit the extent, but not the TP-alignment slack.
  check(plan_startup_tls({overflow, 5}, large_storage, set) ==
        TLSLayoutError::Overflow);
  check(!set.modules && large_storage[0].module_id == 99);
  // Replanning preserves deterministic IDs; no process-global ID counter.
  check(plan_startup_tls(mixed, modules, set) == TLSLayoutError::None);
  check(modules[0].module_id == 1 && modules[2].module_id == 3);
  return 0;
}
