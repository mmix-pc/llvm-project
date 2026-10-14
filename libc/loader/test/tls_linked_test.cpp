//===-- Host relocation of linked MMIX TLS images -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/relocation.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace LIBC_NAMESPACE::loader;
namespace cpp = LIBC_NAMESPACE::cpp;

static void check(bool condition, int line = __builtin_LINE()) {
  if (!condition) {
    std::fprintf(stderr, "linked TLS check failed at line %d\n", line);
    std::abort();
  }
}

struct Input {
  std::vector<unsigned char> file;
  std::vector<std::vector<unsigned char>> memory;
  std::vector<MappedRegion> maps;
  std::vector<WritableRegion> writes;
  ElfImage image;
  DynamicInfo metadata;
  void load(const char *path, unsigned object) {
    bool main = object == 0;
    FILE *f = std::fopen(path, "rb");
    check(f && !std::fseek(f, 0, SEEK_END));
    long size = std::ftell(f);
    check(size > 64 && !std::fseek(f, 0, SEEK_SET));
    file.resize(size);
    check(std::fread(file.data(), 1, file.size(), f) == file.size());
    check(!std::fclose(f));
    for (unsigned i = 40; i < 48; ++i)
      file[i] = 0;
    for (unsigned i = 58; i < 64; ++i)
      file[i] = 0;
    auto role = !main                 ? ImageRole::SharedObject
                : file[17] == ET_EXEC ? ImageRole::MainExecutable
                                      : ImageRole::MainPIE;
    Address bias =
        role == ImageRole::MainExecutable ? 0 : 0x1000000 + object * 0x2000000;
    check(ElfImage::open_file({file.data(), file.size()}, role, bias, 8192,
                              image) == ElfError::None);
    Address header = image.header_address(), table = image.table_address();
    memory.reserve(image.header()->e_phnum);
    bool relro = false;
    for (unsigned i = 0; i < image.header()->e_phnum; ++i) {
      Elf64_Phdr p;
      check(image.program_header(i, p));
      relro |= p.p_type == PT_GNU_RELRO;
      if (p.p_type != PT_LOAD || !p.p_memsz)
        continue;
      memory.emplace_back(p.p_memsz, 0);
      auto &bytes = memory.back();
      for (Address j = 0; j < p.p_filesz; ++j)
        bytes[j] = file[p.p_offset + j];
      maps.push_back({p.p_vaddr, {bytes.data(), bytes.size()}});
      if (p.p_flags & PF_W)
        writes.push_back({p.p_vaddr, {bytes.data(), bytes.size()}});
    }
    check(relro);
    check(ElfImage::open_mapped({maps.data(), maps.size()}, header, table, role,
                                bias, 8192, image) == ElfError::None);
    check(DynamicInfo::open(image, metadata) == DynamicError::None);
  }
};

static std::string name(const DynamicInfo &metadata, const Elf64_Sym &symbol) {
  Bytes bytes;
  check(metadata.string(symbol.st_name, bytes));
  return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

int main(int argc, char **argv) {
  check(argc == 4);
  Input input[3];
  for (unsigned i = 0; i < 3; ++i)
    input[i].load(argv[i + 1], i);
  const ElfImage *images[] = {&input[0].image, &input[1].image,
                              &input[2].image};
  StartupTLSModule modules[3];
  StartupTLSSet layout;
  check(plan_startup_tls(images, modules, layout) == TLSLayoutError::None);
  Address offsets[3], cursor = 16;
  for (unsigned i = 0; i < 3; ++i) {
    const auto *tls = images[i]->tls_header();
    check(tls);
    Address alignment = tls->p_align ? tls->p_align : 1;
    while (cursor % alignment != tls->p_vaddr % alignment)
      ++cursor;
    offsets[i] = cursor;
    cursor += tls->p_memsz;
    check(modules[i].tp_offset == offsets[i]);
  }
  struct Expected {
    unsigned image;
    Address offset, value;
  };
  std::vector<Expected> expected;
  bool seen[3] = {};
  for (unsigned i = 0; i < 3; ++i) {
    const auto &metadata = input[i].metadata;
    for (Address j = 0; j < metadata.relocation_count(); ++j) {
      DynamicRelocation r;
      check(metadata.relocation(j, r));
      unsigned type = static_cast<unsigned>(r.info), provider = i;
      Elf64_Sym symbol{};
      Address index = r.info >> 32;
      if (index) {
        check(metadata.symbol(index, symbol));
        if (symbol.st_other == STV_DEFAULT &&
            (symbol.st_info >> 4) != STB_LOCAL) {
          auto wanted = name(metadata, symbol);
          bool found = false;
          for (unsigned k = 0; k < 3 && !found; ++k)
            for (Address s = 1; s < input[k].metadata.symbol_count(); ++s) {
              Elf64_Sym candidate;
              check(input[k].metadata.symbol(s, candidate));
              if (candidate.st_shndx != SHN_UNDEF &&
                  candidate.st_other == STV_DEFAULT &&
                  name(input[k].metadata, candidate) == wanted) {
                symbol = candidate;
                provider = k;
                found = true;
                break;
              }
            }
          check(found);
        }
      }
      Address value;
      if (type == 41)
        value = images[i]->load_bias() + r.addend;
      else if (type >= 44 && type <= 46) {
        seen[type - 44] = true;
        value = type == 44 ? provider + 1 : symbol.st_value + r.addend;
        if (type == 46)
          value += offsets[provider];
      } else {
        check(type == 5 || type == 42);
        value = symbol.st_value + r.addend;
        if (symbol.st_shndx != SHN_ABS)
          value += images[provider]->load_bias();
      }
      expected.push_back({i, r.offset, value});
    }
  }
  check(seen[0] && seen[1] && seen[2]);
  SymbolObject objects[] = {{images[0], &input[0].metadata},
                            {images[1], &input[1].metadata},
                            {images[2], &input[2].metadata}};
  cpp::span<const WritableRegion> writable[] = {
      {input[0].writes.data(), input[0].writes.size()},
      {input[1].writes.data(), input[1].writes.size()},
      {input[2].writes.data(), input[2].writes.size()}};
  RelocatedTLS ready;
  check(relocate_startup_tls(objects, writable, layout, ready).error ==
        StartupRelocationError::None);
  check(ready.modules() == &layout);
  for (auto e : expected) {
    Bytes bytes =
        images[e.image]->mapped_memory_bytes(e.offset, 8, PF_R | PF_W);
    check(bytes.size() == 8);
    Address actual = 0;
    for (unsigned char byte : bytes)
      actual = (actual << 8) | byte;
    check(actual == e.value);
  }
  return 0;
}
