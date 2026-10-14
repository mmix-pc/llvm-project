//===-- Checked startup TLS metadata tests --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/dynamic.h"
#include "loader/linux/symbols.h"
#include "loader/linux/target.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace loader_test;

static void check(bool condition, int line = __builtin_LINE()) {
  if (!condition) {
    std::fprintf(stderr, "TLS metadata check failed at line %d\n", line);
    std::abort();
  }
}

struct TLSFixture : DynamicFixture {
  explicit TLSFixture(ImageRole role = ImageRole::SharedObject)
      : DynamicFixture(role) {
    // Nonzero alignment residue and a BSS tail; no section table.
    segment(role == ImageRole::MainPIE ? 7 : 4, PT_TLS, PF_R, 0x4a08, 0x4a08, 8,
            64, 32);
    put(0x81c, 1, (STB_GLOBAL << 4) | STT_TLS);
    put(0x820, 8, 8);
    put(0x828, 8, 8);
    put(0xb20, 8, (Address(1) << 32) | 45);
  }
};

static void fixtures() {
  ElfImage image;
  DynamicInfo info;
  TLSFixture f;
  check(ElfImage::open_file(f.view(), ImageRole::SharedObject, bias, page,
                            image) == ElfError::None);
  check(image.tls_header() && image.tls_header()->p_memsz == 64);
  check(DynamicInfo::open(image, info) == DynamicError::None);
  check(info.requires_tls());
  TLSFixture main(ImageRole::MainPIE);
  ElfImage main_image;
  check(ElfImage::open_file(main.view(), ImageRole::MainPIE, bias, page,
                            main_image) == ElfError::None);
  DynamicInfo main_info;
  check(DynamicInfo::open(main_image, main_info) == DynamicError::None);
  check(main_info.requires_tls());
  SymbolObject object{&image, &info};
  StartupSymbols scope;
  check(StartupSymbols::open({&object, 1}, scope) == SymbolError::Scope);
  Address value;
  check(!runtime_relocation_value((Address(1) << 32) | 45, bias, 0, value));
  MappedRegion region{0, f.view()};
  check(ElfImage::open_mapped({&region, 1}, 0, 64, ImageRole::SharedObject,
                              bias, page, image) == ElfError::None);
  check(DynamicInfo::open(image, info) == DynamicError::None);
  region.bytes = {f.bytes, 0x4a08};
  check(ElfImage::open_mapped({&region, 1}, 0, 64, ImageRole::SharedObject,
                              bias, page, image) == ElfError::TLS);

  struct Mutation {
    unsigned at, width;
    Address value;
  };
  const Mutation bad_headers[] = {
      {ph(4, 4), 4, PF_R | PF_X},
      {ph(4, 32), 8, 65},
      {ph(4, 48), 8, 3},
      {ph(4, 48), 8, Address(1) << 63},
      {ph(4, 8), 8, 0x4a09},
      {ph(4, 8), 8, 0x6008},
      {ph(4, 8), 8, ~Address(0) - 23},
      {ph(4, 16), 8, ~Address(0) - 23},
      {ph(4, 40), 8, ~Address(0)},
      {ph(7), 4, PT_TLS}, // Duplicate even when each header is valid.
  };
  for (auto m : bad_headers) {
    TLSFixture bad;
    bad.put(m.at, m.width, m.value);
    check(ElfImage::open_file(bad.view(), ImageRole::SharedObject, bias, page,
                              image) == ElfError::TLS);
    check(!image.tls_header());
  }
  const Mutation bad_metadata[] = {
      {ph(4), 4, PT_NULL},
      {0x81e, 2, SHN_ABS},
      {0x81c, 1, STT_COMMON},
      {0x820, 8, 65},
      {0x828, 8, 57},
      {0x828, 8, ~Address(0)},
      {0xb18, 8, 0x4809},
      {0xb18, 8, 0x2000},
      {0xb18, 8, 0x4000},
      {0xb18, 8, 0x4a08},
      {0xb18, 8, 0x4800},
      {0xb20, 8, (Address(2) << 32) | 45},
      {0xb20, 8, (Address(1) << 32) | 42},
      {0xb20, 8, 45},
  };
  for (auto m : bad_metadata) {
    TLSFixture bad;
    bad.put(m.at, m.width, m.value);
    check(ElfImage::open_file(bad.view(), ImageRole::SharedObject, bias, page,
                              image) == ElfError::None);
    check(DynamicInfo::open(image, info) != DynamicError::None);
    check(!info.requires_tls() && !info.symbol_count());
  }
  for (unsigned type : {44, 45, 46}) {
    TLSFixture input;
    input.put(0xb20, 8, (Address(1) << 32) | type);
    check(ElfImage::open_file(input.view(), ImageRole::SharedObject, bias, page,
                              image) == ElfError::None);
    check(DynamicInfo::open(image, info) == DynamicError::None);
    if (type != 45) {
      input.put(0xb20, 8, type); // Current-module forms.
      check(DynamicInfo::open(image, info) == DynamicError::None);
    }
    if (type == 44) {
      input.put(0xb28, 8, 1);
      check(DynamicInfo::open(image, info) == DynamicError::Relocation);
    }
  }
  TLSFixture imported;
  imported.put(ph(4), 4, PT_NULL);
  imported.put(0x81e, 2, SHN_UNDEF);
  imported.put(0x820, 8, 0);
  check(ElfImage::open_file(imported.view(), ImageRole::SharedObject, bias,
                            page, image) == ElfError::None);
  check(DynamicInfo::open(image, info) == DynamicError::None);
  check(info.requires_tls());
  imported.put(0xb20, 8, 44);
  check(DynamicInfo::open(image, info) == DynamicError::Relocation);

  for (auto role : {ImageRole::SharedObject, ImageRole::MainPIE,
                    ImageRole::MainExecutable, ImageRole::Interpreter}) {
    Fixture input(role);
    Address v = role == ImageRole::MainExecutable ? base : 0;
    for (unsigned alignment : {0, 1, 32}) {
      // An empty template is valid, including pure .tbss and empty PT_TLS.
      for (unsigned size : {0, 64}) {
        input.segment(7, PT_TLS, PF_R, 0x4a08, v + 0x4a08, 0, size, alignment);
        check(
            ElfImage::open_file(input.view(), role, v ? 0 : bias, page,
                                image) ==
            (role == ImageRole::Interpreter ? ElfError::TLS : ElfError::None));
      }
    }
  }
  DynamicFixture static_tls;
  static_tls.set(DT_FLAGS, DF_BIND_NOW | DF_STATIC_TLS);
  check(ElfImage::open_file(static_tls.view(), ImageRole::SharedObject, bias,
                            page, image) == ElfError::None);
  check(DynamicInfo::open(image, info) == DynamicError::None);
  check(info.requires_tls());
}

// Actual linker artifacts exercise the same parser without section headers.
// These are host checks of target bytes, not target TLS execution.
static void linked(const char *path) {
  FILE *input = std::fopen(path, "rb");
  check(input && std::fseek(input, 0, SEEK_END) == 0);
  long size = std::ftell(input);
  check(size > 0 && std::fseek(input, 0, SEEK_SET) == 0);
  std::vector<unsigned char> file(static_cast<size_t>(size));
  check(std::fread(file.data(), 1, file.size(), input) == file.size());
  check(std::fclose(input) == 0);
  for (unsigned i = 40; i < 48; ++i)
    file[i] = 0;
  for (unsigned i = 58; i < 64; ++i)
    file[i] = 0;
  ElfImage image;
  DynamicInfo info;
  check(ElfImage::open_file({file.data(), file.size()}, ImageRole::SharedObject,
                            bias, page, image) == ElfError::None);
  check(image.tls_header());
  check(DynamicInfo::open(image, info) == DynamicError::None);
  check(info.requires_tls());
  Address tls_offset = 0;
  Address header_address = image.header_address();
  Address table_address = image.table_address();
  std::vector<std::vector<unsigned char>> storage;
  std::vector<MappedRegion> mappings;
  storage.reserve(image.header()->e_phnum);
  for (unsigned i = 0; i < image.header()->e_phnum; ++i) {
    Elf64_Phdr p;
    check(image.program_header(i, p));
    if (p.p_type == PT_TLS)
      tls_offset = image.header()->e_phoff + 56 * i;
    if (p.p_type == PT_LOAD && p.p_memsz) {
      storage.emplace_back(static_cast<size_t>(p.p_memsz), 0);
      auto &bytes = storage.back();
      for (Address j = 0; j < p.p_filesz; ++j)
        bytes[j] = file[p.p_offset + j];
      mappings.push_back({p.p_vaddr, {bytes.data(), bytes.size()}});
    }
  }
  check(tls_offset);
  bool mutated_symbol = false, mutated_relocation = false;
  for (Address i = 1; i < info.symbol_count(); ++i) {
    Elf64_Sym symbol;
    check(info.symbol(i, symbol));
    if ((symbol.st_info & 15) != STT_TLS || symbol.st_shndx == SHN_UNDEF)
      continue;
    Bytes bytes =
        image.file_backed_bytes(info.value(DynamicTag::Symtab) + i * 24 + 8, 8);
    check(bytes.size() == 8);
    auto *value = file.data() + (bytes.data() - file.data());
    unsigned char saved[8];
    for (unsigned j = 0; j < 8; ++j) {
      saved[j] = value[j];
      value[j] = 255;
    }
    DynamicInfo rejected;
    check(DynamicInfo::open(image, rejected) == DynamicError::Symbol);
    for (unsigned j = 0; j < 8; ++j)
      value[j] = saved[j];
    mutated_symbol = true;
    break;
  }
  for (Address i = 0; i < info.relocation_count(); ++i) {
    DynamicRelocation relocation;
    check(info.relocation(i, relocation));
    if (!is_tls_relocation(relocation.info))
      continue;
    Bytes bytes =
        image.file_backed_bytes(info.value(DynamicTag::Rela) + i * 24, 8);
    check(bytes.size() == 8);
    auto *value = file.data() + (bytes.data() - file.data());
    value[7] ^= 1;
    DynamicInfo rejected;
    check(DynamicInfo::open(image, rejected) == DynamicError::Relocation);
    value[7] ^= 1;
    mutated_relocation = true;
    break;
  }
  check(mutated_symbol && mutated_relocation);
  check(ElfImage::open_mapped(
            {mappings.data(), mappings.size()}, header_address, table_address,
            ImageRole::SharedObject, bias, page, image) == ElfError::None);
  check(DynamicInfo::open(image, info) == DynamicError::None);
  SymbolObject object{&image, &info};
  StartupSymbols scope;
  check(StartupSymbols::open({&object, 1}, scope) == SymbolError::Scope);
  // Real PT_TLS: reject a non-power-of-two alignment and overflowing extent.
  for (unsigned field : {40, 48}) {
    auto bad = file;
    for (unsigned j = 0; j < 8; ++j)
      bad[tls_offset + field + j] = field == 40 ? 255 : 0;
    if (field == 48)
      bad[tls_offset + field + 7] = 3;
    check(ElfImage::open_file({bad.data(), bad.size()}, ImageRole::SharedObject,
                              bias, page, image) == ElfError::TLS);
  }
}

int main(int argc, char **argv) {
  fixtures();
  for (int i = 1; i < argc; ++i)
    linked(argv[i]);
  return 0;
}
