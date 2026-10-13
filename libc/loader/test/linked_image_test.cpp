//===-- Native validation of a linked bootstrap fixture -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/self_relocation.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace LIBC_NAMESPACE::loader;

static void check(bool condition) {
  if (!condition)
    std::abort();
}

static Address read_octa(const unsigned char *bytes) {
  Address result = 0;
  for (unsigned i = 0; i < 8; ++i)
    result = (result << 8) | bytes[i];
  return result;
}

int main(int argc, char **argv) {
  check(argc == 2);
  FILE *input = std::fopen(argv[1], "rb");
  check(input && std::fseek(input, 0, SEEK_END) == 0);
  long size = std::ftell(input);
  check(size > 0 && std::fseek(input, 0, SEEK_SET) == 0);
  std::vector<unsigned char> file(static_cast<size_t>(size));
  check(std::fread(file.data(), 1, file.size(), input) == file.size());
  check(std::fclose(input) == 0);
  constexpr Address bias = 0x1000000, page = 8192;
  ElfImage image;
  check(ElfImage::open_file({file.data(), file.size()}, ImageRole::Interpreter,
                            bias, page, image) == ElfError::None);
  const auto header = *image.header();
  check(header.e_phoff + Address(header.e_phnum) * 56 <= page);
  std::vector<std::vector<unsigned char>> storage;
  storage.reserve(header.e_phnum);
  std::vector<MappedRegion> mapped;
  std::vector<WritableRegion> writable;
  Elf64_Phdr dynamic = {};
  for (unsigned i = 0; i < header.e_phnum; ++i) {
    Elf64_Phdr p;
    check(image.program_header(i, p));
    if (p.p_type == PT_DYNAMIC)
      dynamic = p;
    if (p.p_type != PT_LOAD || !p.p_memsz)
      continue;
    storage.emplace_back(static_cast<size_t>(p.p_memsz), 0);
    auto &bytes = storage.back();
    for (Address j = 0; j < p.p_filesz; ++j)
      bytes[j] = file[p.p_offset + j];
    mapped.push_back({p.p_vaddr, {bytes.data(), bytes.size()}});
    if (p.p_flags & PF_W)
      writable.push_back({p.p_vaddr, {bytes.data(), bytes.size()}});
  }
  check(ElfImage::open_mapped({mapped.data(), mapped.size()}, 0, header.e_phoff,
                              ImageRole::Interpreter, bias, page,
                              image) == ElfError::None);
  Bytes dyn = image.file_backed_bytes(dynamic.p_vaddr, dynamic.p_filesz);
  Address rela = 0, relasz = 0;
  for (Address i = 0; i < dyn.size(); i += 16) {
    Address tag = read_octa(dyn.data() + i);
    if (tag == DT_NULL)
      break;
    if (tag == DT_RELA)
      rela = read_octa(dyn.data() + i + 8);
    if (tag == DT_RELASZ)
      relasz = read_octa(dyn.data() + i + 8);
  }
  check(relasz != 0);
  check(relocate_self(image, {writable.data(), writable.size()}) ==
        RelocationError::None);
  Bytes table = image.file_backed_bytes(rela, relasz);
  for (Address i = 0; i < table.size(); i += 24) {
    Address offset = read_octa(table.data() + i);
    Address addend = read_octa(table.data() + i + 16);
    Bytes value = image.mapped_memory_bytes(offset, 8, PF_R | PF_W);
    check(value.size() == 8 && read_octa(value.data()) == bias + addend);
  }
  std::puts("linked bootstrap: mapped admission and RELATIVE values passed");
}
