//===-- Dynamic metadata fixture --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_TEST_DYNAMIC_FIXTURE_H
#define LLVM_LIBC_LOADER_TEST_DYNAMIC_FIXTURE_H

#include "elf_fixture.h"

namespace loader_test {
struct DynamicFixture : Fixture {
  unsigned entries = 0;
  explicit DynamicFixture(ImageRole role = ImageRole::SharedObject)
      : Fixture(role) {
    const char names[] = "\0foo\0libx.so\0self.so\0$ORIGIN\0";
    for (unsigned i = 0; i < sizeof(names); ++i)
      bytes[0x700 + i] = static_cast<unsigned char>(names[i]);
    put(0x818, 4, 1);
    bytes[0x81c] = (STB_GLOBAL << 4) | STT_FUNC;
    put(0x81e, 2, 1);
    put(0x820, 8, 0x2000);
    put(0x828, 8, 4);
    // One-bucket SysV table, null and foo symbols.
    put(0x900, 4, 1);
    put(0x904, 4, 2);
    put(0x908, 4, 1);
    // One-bucket GNU table, one bloom word, hash("foo") == 0x0b887389.
    put(0xa00, 4, 1);
    put(0xa04, 4, 1);
    put(0xa08, 4, 1);
    put(0xa0c, 4, 5);
    put(0xa10, 8,
        (Address(1) << (0x0b887389 % 64)) |
            (Address(1) << ((0x0b887389 >> 5) % 64)));
    put(0xa18, 4, 1);
    put(0xa1c, 4, 0x0b887389);
    put(0xb00, 8, 0x4800);
    put(0xb08, 8, 41);
    put(0xb10, 8, 0x2000);
    put(0xb18, 8, 0x4808);
    put(0xb20, 8, (Address(1) << 32) | 42);
    add(DT_STRTAB, 0x700);
    add(DT_STRSZ, sizeof(names));
    add(DT_SYMTAB, 0x800);
    add(DT_SYMENT, 24);
    add(DT_HASH, 0x900);
    add(DT_GNU_HASH, 0xa00);
    add(DT_RELA, 0xb00);
    add(DT_RELASZ, 48);
    add(DT_RELAENT, 24);
    add(DT_RELACOUNT, 1);
    add(DT_NEEDED, 5);
    add(DT_NEEDED, 5);
    add(DT_SONAME, 13);
    add(DT_RPATH, 21);
    add(DT_RUNPATH, 21);
    add(DT_INIT, 0x2000);
    add(DT_FINI, 0x2004);
    add(DT_INIT_ARRAY, 0x4900);
    add(DT_INIT_ARRAYSZ, 16);
    add(DT_FINI_ARRAY, 0x4910);
    add(DT_FINI_ARRAYSZ, 8);
    add(DT_FLAGS, DF_BIND_NOW);
    add(DT_FLAGS_1, DF_1_NOW | DF_1_NODELETE);
    add(DT_BIND_NOW, 0);
    add(DT_CHECKSUM, 17);
    add(DT_NULL, 0);
    segment(5, PT_DYNAMIC, PF_R | PF_W, 0x4000, 0x4000, entries * 16,
            entries * 16, 8);
  }
  void add(Address tag, Address value) {
    put(0x4000 + entries * 16, 8, tag);
    put(0x4008 + entries++ * 16, 8, value);
  }
  unsigned at(Address tag) const {
    for (unsigned i = 0; i < entries; ++i) {
      Address value = 0;
      for (unsigned j = 0; j < 8; ++j)
        value = (value << 8) | bytes[0x4000 + i * 16 + j];
      if (value == tag)
        return 0x4000 + i * 16;
    }
    return 0;
  }
  void set(Address tag, Address value) { put(at(tag) + 8, 8, value); }
};
} // namespace loader_test
#endif
