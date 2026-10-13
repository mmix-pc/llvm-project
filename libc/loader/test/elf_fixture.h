//===-- Loader ELF test fixture ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_TEST_ELF_FIXTURE_H
#define LLVM_LIBC_LOADER_TEST_ELF_FIXTURE_H

#include "loader/linux/elf.h"

namespace loader_test {
using namespace LIBC_NAMESPACE::loader;

constexpr Address page = 8192, base = 0x100000, bias = 0x400000;
constexpr unsigned ph(unsigned i, unsigned field = 0) {
  return 64 + 56 * i + field;
}

struct Fixture {
  unsigned char bytes[0x6000] = {};
  bool big = true;
  void put(unsigned at, unsigned width, Address value) {
    for (unsigned i = 0; i < width; ++i) {
      bytes[at + (big ? width - i - 1 : i)] = value & 255;
      value >>= 8;
    }
  }
  void segment(unsigned index, unsigned type, unsigned flags, Address offset,
               Address vaddr, Address filesz, Address memsz, Address align) {
    put(ph(index), 4, type);
    put(ph(index, 4), 4, flags);
    put(ph(index, 8), 8, offset);
    put(ph(index, 16), 8, vaddr);
    put(ph(index, 24), 8, ~Address(0)); // p_paddr has no admission meaning.
    put(ph(index, 32), 8, filesz);
    put(ph(index, 40), 8, memsz);
    put(ph(index, 48), 8, align);
  }
  explicit Fixture(ImageRole role = ImageRole::MainPIE, bool big_endian = true)
      : big(big_endian) {
    bool main = role == ImageRole::MainPIE || role == ImageRole::MainExecutable;
    Address v = role == ImageRole::MainExecutable ? base : 0;
    bytes[0] = 0x7f;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[EI_CLASS] = ELFCLASS64;
    bytes[EI_DATA] = big ? ELFDATA2MSB : ELFDATA2LSB;
    bytes[EI_VERSION] = EV_CURRENT;
    put(16, 2, role == ImageRole::MainExecutable ? ET_EXEC : ET_DYN);
    put(18, 2, EM_MMIX);
    put(20, 4, EV_CURRENT);
    put(24, 8, role == ImageRole::SharedObject ? 0 : v + 0x2000);
    put(32, 8, 64);
    put(52, 2, 64);
    put(54, 2, 56);
    put(56, 2, 8);
    segment(0, PT_PHDR, PF_R, 64, v + 64, 448, 448, 8);
    segment(1, PT_LOAD, PF_R, 0, v, 0x1000, page, page);
    segment(2, PT_LOAD, PF_R | PF_X, 0x2000, v + 0x2000, 0x100, page, page);
    segment(3, PT_LOAD, PF_R | PF_W, 0x4000, v + 0x4000, 0x1000, page, page);
    constexpr char path[] = "/lib/ld-llvm-mmix.so.1";
    for (unsigned i = 0; i < sizeof(path); ++i)
      bytes[0x300 + i] = path[i];
    segment(4, main ? PT_INTERP : PT_NULL, PF_R, 0x300, v + 0x300, sizeof(path),
            sizeof(path), 1);
    segment(5, PT_DYNAMIC, PF_R | PF_W, 0x4000, v + 0x4000, 32, 32, 8);
    put(0x4000, 8, DT_STRTAB);
    segment(6, PT_GNU_STACK, PF_R | PF_W, 0, 0, 0, 0, 16);
    segment(7, PT_GNU_RELRO, PF_R, 0x4000, v + 0x4000, page, page, 1);
  }
  Bytes view() const { return Bytes(bytes, sizeof(bytes)); }
};
} // namespace loader_test
#endif
