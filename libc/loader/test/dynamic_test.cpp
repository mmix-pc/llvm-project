//===-- Dynamic metadata tests --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/dynamic.h"
#include <stdio.h>

using namespace loader_test;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                  \
      return 1;                                                                \
    }                                                                          \
  } while (false)

extern "C" int main() {
  DynamicFixture f;
  ElfImage image;
  DynamicInfo info;
  CHECK(ElfImage::open_file(f.view(), ImageRole::SharedObject, bias, page,
                            image) == ElfError::None);
  CHECK(DynamicInfo::open(image, info) == DynamicError::None);
  CHECK(info.symbol_count() == 2 && info.needed_count() == 2 &&
        info.relocation_count() == 2);
  Bytes name;
  CHECK(info.needed(0, name) && name.size() == 7 && name[0] == 'l');
  CHECK(info.needed(1, name) && !info.needed(2, name));
  CHECK(info.string(0, name) && name.empty());
  CHECK(!info.string(~Address(0), name));
  Elf64_Sym symbol;
  CHECK(info.symbol(1, symbol) && symbol.st_value == 0x2000 &&
        !info.symbol(2, symbol));
  DynamicRelocation rela;
  CHECK(info.relocation(1, rela) && rela.offset == 0x4808 &&
        !info.relocation(2, rela));
  CHECK(info.lifecycle_array(DynamicTag::InitArray).size() == 16);
  CHECK(info.sysv_hash().size() == 20 && info.gnu_hash().size() == 32);
  MappedRegion mapping{0, f.view()};
  CHECK(ElfImage::open_mapped({&mapping, 1}, 0, 64, ImageRole::SharedObject,
                              bias, page, image) == ElfError::None);
  CHECK(DynamicInfo::open(image, info) == DynamicError::None);

  // Check all orders, duplicates, adjacency and partial overlaps against an
  // independent byte-occupancy oracle. A lower destination must not reset the
  // high-water mark or hide a collision with a nonadjacent relocation.
  const unsigned destinations[] = {0, 1, 7, 8, 9, 16, 24};
  for (unsigned a : destinations) {
    for (unsigned b : destinations) {
      for (unsigned c : destinations) {
        DynamicFixture input;
        input.set(DT_RELASZ, 72);
        const unsigned offsets[] = {a, b, c};
        bool occupied[32] = {};
        bool collision = false;
        for (unsigned i = 0; i < 3; ++i) {
          input.put(0xb00 + i * 24, 8, 0x4800 + offsets[i]);
          input.put(0xb08 + i * 24, 8, 41);
          for (unsigned byte = offsets[i]; byte < offsets[i] + 8; ++byte) {
            collision |= occupied[byte];
            occupied[byte] = true;
          }
        }
        CHECK(ElfImage::open_file(input.view(), ImageRole::SharedObject, bias,
                                 page, image) == ElfError::None);
        CHECK(DynamicInfo::open(image, info) ==
              (collision ? DynamicError::Relocation : DynamicError::None));
      }
    }
  }

  MappedRegion partial[] = {{0, {f.bytes, 0xc00}},
                            {0x4000, {f.bytes + 0x4000, 0x1000}}};
  CHECK(ElfImage::open_mapped({partial, 2}, 0, 64, ImageRole::SharedObject,
                              bias, page, image) == ElfError::None);
  CHECK(DynamicInfo::open(image, info) == DynamicError::None);
  DynamicFixture crossing;
  crossing.put(ph(1, 32), 8, page);
  for (unsigned i = 0; i < 20; ++i)
    crossing.bytes[0x1ff8 + i] = crossing.bytes[0x900 + i];
  crossing.set(DT_HASH, 0x1ff8);
  CHECK(ElfImage::open_file(crossing.view(), ImageRole::SharedObject, bias,
                            page, image) == ElfError::None);
  CHECK(DynamicInfo::open(image, info) == DynamicError::None);

  struct Mutation {
    unsigned offset, width;
    Address value;
  };
  const Mutation bad[] = {
      {0x700, 1, 1},
      {0x81b, 1, 255},                  // Invalid strings/name index.
      {0x81c, 1, (10 << 4) | STT_FUNC}, // GNU_UNIQUE.
      {0x81c, 1, (STB_GLOBAL << 4) | STT_TLS},
      {0x81c, 1, (STB_GLOBAL << 4) | STT_GNU_IFUNC},
      {0x81d, 1, 0x80},
      {0x81e, 2, SHN_COMMON},
      {0x81e, 2, SHN_XINDEX},
      {0x820, 8, 0x4000},
      {0x828, 8, ~Address(0)},
      {0x800, 4, 1},
      {0x900, 4, 0},
      {0x904, 4, 0},
      {0x904, 4, 0xffffffff},
      {0x908, 4, 0},
      {0x908, 4, 2},
      {0x90c, 4, 1},
      {0x910, 4, 1}, // SysV index/cycle.
      {0xa00, 4, 0},
      {0xa04, 4, 0},
      {0xa04, 4, 3},
      {0xa08, 4, 0},
      {0xa08, 4, 3},
      {0xa08, 4, 0x80000000},
      {0xa0c, 4, 32},
      {0xa10, 8, 0},
      {0xa18, 4, 0},
      {0xa18, 4, 2},
      {0xa1c, 4, 0x0b887388},
      {0xa1c, 4, 1}, // Unterminated/wrong GNU hash.
      {0xb00, 8, 0x2000},
      {0xb00, 8, ~Address(0)},
      {0xb00, 8, 0x4000},
      {0xb18, 8, 0x4804},
      {0xb08, 8, (Address(1) << 32) | 41},
      {0xb20, 8, (Address(2) << 32) | 42},
      {0xb20, 8, (Address(1) << 32) | 44},
      {0xb20, 8, 0},
  };
  for (const auto &m : bad) {
    DynamicFixture input;
    input.put(m.offset, m.width, m.value);
    CHECK(ElfImage::open_file(input.view(), ImageRole::SharedObject, bias, page,
                              image) == ElfError::None);
    CHECK(DynamicInfo::open(image, info) != DynamicError::None);
    CHECK(!info.symbol_count() && !info.needed_count() &&
          !info.relocation_count());
    CHECK(!info.symbol(0, symbol) && !info.has(DynamicTag::Strtab));
  }
  struct TagMutation {
    Address tag, value;
  };
  const TagMutation bad_tags[] = {
      {DT_STRSZ, 0},           {DT_STRSZ, ~Address(0)}, {DT_STRTAB, 0x4fff},
      {DT_SYMENT, 16},         {DT_SYMTAB, 0x4ff0},     {DT_HASH, 0x5fff},
      {DT_GNU_HASH, 0x5fff},   {DT_NEEDED, 0},          {DT_NEEDED, 0xffff},
      {DT_SONAME, 0xffff},     {DT_RUNPATH, 0xffff},    {DT_RELASZ, 47},
      {DT_RELAENT, 8},         {DT_RELA, 0x4fff},       {DT_RELACOUNT, 2},
      {DT_FLAGS, DF_TEXTREL},  {DT_FLAGS_1, 0x10},      {DT_FLAGS_1, DF_1_PIE},
      {DT_INIT, 0x4000},       {DT_FINI, 0x2001},       {DT_INIT_ARRAYSZ, 7},
      {DT_FINI_ARRAY, 0x5ff8},
  };
  for (auto m : bad_tags) {
    DynamicFixture input;
    input.set(m.tag, m.value);
    CHECK(ElfImage::open_file(input.view(), ImageRole::SharedObject, bias, page,
                              image) == ElfError::None);
    CHECK(DynamicInfo::open(image, info) != DynamicError::None);
  }
  const Address unsupported[] = {
      DT_SYMBOLIC, DT_TEXTREL,  DT_REL,         DT_RELR,        DT_JMPREL,
      DT_PLTREL,   DT_PLTRELSZ, DT_VERSYM,      DT_VERDEF,      DT_VERNEED,
      DT_AUDIT,    DT_DEPAUDIT, DT_TLSDESC_PLT, DT_TLSDESC_GOT, 0x70000001};
  for (Address tag : unsupported) {
    DynamicFixture input;
    input.put(input.at(DT_CHECKSUM), 8, tag);
    CHECK(ElfImage::open_file(input.view(), ImageRole::SharedObject, bias, page,
                              image) == ElfError::None);
    CHECK(DynamicInfo::open(image, info) == DynamicError::Unsupported);
  }
  // Both hash formats are independently sufficient; the file is sectionless.
  const Address hashes[] = {DT_HASH, DT_GNU_HASH};
  for (Address tag : hashes) {
    DynamicFixture input;
    unsigned at = input.at(tag);
    input.put(at, 8, DT_NEEDED);
    input.put(at + 8, 8, 5);
    CHECK(ElfImage::open_file(input.view(), ImageRole::SharedObject, bias, page,
                              image) == ElfError::None);
    CHECK(DynamicInfo::open(image, info) == DynamicError::None);
  }
  const Address required[] = {DT_STRTAB,     DT_STRSZ,       DT_SYMTAB,
                              DT_SYMENT,     DT_RELA,        DT_RELASZ,
                              DT_RELAENT,    DT_INIT_ARRAY,  DT_INIT_ARRAYSZ,
                              DT_FINI_ARRAY, DT_FINI_ARRAYSZ};
  for (Address tag : required) {
    DynamicFixture input;
    unsigned at = input.at(tag);
    input.put(at, 8, DT_NEEDED);
    input.put(at + 8, 8, 5);
    CHECK(ElfImage::open_file(input.view(), ImageRole::SharedObject, bias, page,
                              image) == ElfError::None);
    CHECK(DynamicInfo::open(image, info) != DynamicError::None);
  }
  DynamicFixture no_rela;
  const Address rela_tags[] = {DT_RELA, DT_RELASZ, DT_RELAENT, DT_RELACOUNT};
  for (Address tag : rela_tags) {
    unsigned at = no_rela.at(tag);
    no_rela.put(at, 8, DT_NEEDED);
    no_rela.put(at + 8, 8, 5);
  }
  CHECK(ElfImage::open_file(no_rela.view(), ImageRole::SharedObject, bias, page,
                            image) == ElfError::None);
  CHECK(DynamicInfo::open(image, info) == DynamicError::None &&
        !info.relocation_count());
  DynamicFixture no_hash;
  for (Address tag : hashes) {
    unsigned at = no_hash.at(tag);
    no_hash.put(at, 8, DT_NEEDED);
    no_hash.put(at + 8, 8, 5);
  }
  CHECK(ElfImage::open_file(no_hash.view(), ImageRole::SharedObject, bias, page,
                            image) == ElfError::None);
  CHECK(DynamicInfo::open(image, info) == DynamicError::Hash);
  DynamicFixture preinit;
  preinit.put(preinit.at(DT_INIT_ARRAY), 8, DT_PREINIT_ARRAY);
  preinit.put(preinit.at(DT_INIT_ARRAYSZ), 8, DT_PREINIT_ARRAYSZ);
  CHECK(ElfImage::open_file(preinit.view(), ImageRole::SharedObject, bias, page,
                            image) == ElfError::None);
  CHECK(DynamicInfo::open(image, info) == DynamicError::Lifecycle);
  DynamicFixture duplicate;
  DynamicFixture boundary;
  boundary.bytes[0x81c] = (STB_GLOBAL << 4) | STT_NOTYPE;
  boundary.put(0x820, 8, 0x6000);
  boundary.put(0x828, 8, 0);
  CHECK(ElfImage::open_file(boundary.view(), ImageRole::SharedObject, bias,
                            page, image) == ElfError::None);
  CHECK(DynamicInfo::open(image, info) == DynamicError::None);
  boundary.put(0x820, 8, 0x6001);
  CHECK(DynamicInfo::open(image, info) == DynamicError::Symbol);
  duplicate.put(duplicate.at(DT_CHECKSUM), 8, DT_STRSZ);
  CHECK(ElfImage::open_file(duplicate.view(), ImageRole::SharedObject, bias,
                            page, image) == ElfError::None);
  CHECK(DynamicInfo::open(image, info) == DynamicError::Duplicate);
  DynamicFixture tls;
  tls.put(ph(4), 4, PT_TLS);
  CHECK(ElfImage::open_file(tls.view(), ImageRole::SharedObject, bias, page,
                            image) == ElfError::None);
  CHECK(DynamicInfo::open(image, info) == DynamicError::None);
  CHECK(info.requires_tls());
  DynamicFixture unterminated;
  unterminated.put(unterminated.at(DT_NULL), 8, DT_NEEDED);
  CHECK(ElfImage::open_file(unterminated.view(), ImageRole::SharedObject, bias,
                            page, image) == ElfError::Dynamic);
  DynamicFixture main(ImageRole::MainPIE);
  main.put(main.at(DT_SONAME), 8, DT_DEBUG);
  main.set(DT_DEBUG, 0);
  main.set(DT_FLAGS_1, DF_1_PIE | DF_1_NOW);
  main.put(main.at(DT_INIT_ARRAY), 8, DT_PREINIT_ARRAY);
  main.put(main.at(DT_INIT_ARRAYSZ), 8, DT_PREINIT_ARRAYSZ);
  CHECK(ElfImage::open_file(main.view(), ImageRole::MainPIE, bias, page,
                            image) == ElfError::None);
  CHECK(DynamicInfo::open(image, info) == DynamicError::None);
  CHECK(info.lifecycle_array(DynamicTag::PreinitArray).size() == 16);
  main.set(DT_DEBUG, 1);
  CHECK(ElfImage::open_file(main.view(), ImageRole::MainPIE, bias, page,
                            image) == ElfError::None);
  CHECK(DynamicInfo::open(image, info) == DynamicError::Unsupported);
  // A mapped main can be reparsed after the loader fills DT_DEBUG. The value
  // is metadata only, never a pointer the parser follows.
  MappedRegion main_region{0, main.view()};
  CHECK(ElfImage::open_mapped({&main_region, 1}, 0, 64, ImageRole::MainPIE,
                              bias, page, image) == ElfError::None);
  CHECK(DynamicInfo::open(image, info) == DynamicError::None);
  CHECK(info.value(DynamicTag::Debug) == 1);
  ElfImage absent;
  CHECK(DynamicInfo::open(absent, info) == DynamicError::Image);
  return 0;
}
