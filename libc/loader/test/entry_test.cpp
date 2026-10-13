//===-- Loader initial entry validation tests -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "elf_fixture.h"
#include "loader/linux/entry.h"
#include <stdio.h>

using namespace LIBC_NAMESPACE::loader;
using namespace loader_test;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                  \
      return 1;                                                                \
    }                                                                          \
  } while (false)

constexpr Address sp = 0x700000, self_base = 0x800000;
struct StackFixture {
  unsigned char bytes[512] = {};
  void word(unsigned index, Address value) {
    for (unsigned j = 0; j < 8; ++j) {
      bytes[index * 8 + 7 - j] = value & 255;
      value >>= 8;
    }
  }
  void aux(unsigned index, Address value) { word(6 + index * 2, value); }
  explicit StackFixture(bool executable = false) {
    word(0, 1);
    word(1, sp + 300);
    word(3, sp + 320);
    const Address tags[] = {AT_PHDR, AT_PHENT, AT_PHNUM,  AT_PAGESZ,
                            AT_BASE, AT_ENTRY, AT_RANDOM, AT_SECURE,
                            AT_UID,  AT_EUID,  AT_GID,    AT_EGID};
    Address main_base = executable ? base : bias;
    const Address values[] = {
        main_base + 64, 56, 8, page, self_base, main_base + 0x2000,
        sp + 400,       0,  0, 0,    0,         0};
    for (unsigned i = 0; i < 12; ++i) {
      word(5 + 2 * i, tags[i]);
      aux(i, values[i]);
    }
  }
  Bytes view() const { return Bytes(bytes, sizeof(bytes)); }
};

extern "C" int main() {
  StackFixture stack;
  InitialStack state;
  CHECK(decode_initial_stack(stack.view(), sp, true, state) ==
        EntryError::None);
  CHECK(state.valid && state.original_sp == sp && state.argc == 1);
  CHECK(state.argv == sp + 8 && state.envp == sp + 24 && state.auxv == sp + 40);
  CHECK(state.value(AuxField::Base) != state.value(AuxField::Entry));
  CHECK(state.value(AuxField::Base) != state.value(AuxField::Phdr));
  CHECK(decode_initial_stack(stack.view(), sp + 1, true, state) ==
        EntryError::Stack);
  CHECK(!state.valid);
  CHECK(decode_initial_stack(stack.view(), (Address(1) << 43) - 8, true,
                             state) == EntryError::Stack);
  // Truncate at every byte before the complete AT_NULL pair.
  for (unsigned n = 0; n < 31 * 8; ++n)
    CHECK(decode_initial_stack(Bytes(stack.bytes, n), sp, true, state) !=
          EntryError::None);
  CHECK(decode_initial_stack(Bytes(stack.bytes, 31 * 8), sp, true, state) ==
        EntryError::None);
  CHECK(decode_initial_stack(stack.view(), sp, false, state) !=
        EntryError::None);
  struct Mutation {
    unsigned word;
    Address value;
    EntryError error;
  };
  const Mutation bad[] = {
      {0, ~Address(0), EntryError::Arguments},
      {0, 100, EntryError::Arguments},
      {1, 0, EntryError::Arguments},
      {2, 1, EntryError::Arguments},
      {3, 1, EntryError::Environment},
      {5, AT_IGNORE, EntryError::Missing},
      {7, AT_PHDR, EntryError::Duplicate},
      {6, bias + 65, EntryError::ProgramHeaders},
      {6, (Address(1) << 43) - 8, EntryError::ProgramHeaders},
      {8, 55, EntryError::ProgramHeaders},
      {10, 0, EntryError::ProgramHeaders},
      {10, PN_XNUM, EntryError::ProgramHeaders},
      {10, ~Address(0), EntryError::ProgramHeaders},
      {12, 4096, EntryError::AddressRange},
      {14, 0, EntryError::AddressRange},
      {14, self_base + 1, EntryError::AddressRange},
      {16, 0, EntryError::AddressRange},
      {16, bias + 0x2001, EntryError::AddressRange},
      {18, (Address(1) << 43) - 8, EntryError::AddressRange},
      {20, 1, EntryError::Secure},
      {22, Address(1) << 32, EntryError::Auxv},
      {30, 1, EntryError::Auxv},
  };
  for (const auto &m : bad) {
    StackFixture input;
    input.word(m.word, m.value);
    CHECK(decode_initial_stack(input.view(), sp, true, state) == m.error);
    CHECK(!state.valid);
  }
  // Reject every missing mandatory tag and repeated tags, even equal values.
  for (unsigned i = 0; i < 12; ++i) {
    StackFixture input;
    input.word(5 + 2 * i, AT_IGNORE);
    CHECK(decode_initial_stack(input.view(), sp, true, state) ==
          EntryError::Missing);
    StackFixture repeated;
    repeated.word(29, repeated.bytes[(5 + 2 * i) * 8 + 7]);
    CHECK(decode_initial_stack(repeated.view(), sp, true, state) ==
          EntryError::Duplicate);
  }
  StackFixture duplicate;
  duplicate.word(29, AT_BASE);
  duplicate.word(30, self_base);
  CHECK(decode_initial_stack(duplicate.view(), sp, true, state) ==
        EntryError::Duplicate);
  StackFixture secure;
  secure.aux(7, 1);
  secure.word(3, sp + 8192); // Valid address, not accessible fixture storage.
  CHECK(decode_initial_stack(secure.view(), sp, true, state) ==
        EntryError::Secure);
  StackFixture unknown;
  unknown.word(29, 0x12345);
  unknown.word(30, ~Address(0));
  CHECK(decode_initial_stack(unknown.view(), sp, true, state) ==
        EntryError::None);

  Fixture interpreter(ImageRole::Interpreter);
  MappedRegion self_regions[] = {
      {0, Bytes(interpreter.bytes, 0x1000)},
      {0x2000, Bytes(interpreter.bytes + 0x2000, 0x100)},
      {0x4000, Bytes(interpreter.bytes + 0x4000, 0x1000)}};
  ElfImage self, main;
  CHECK(ElfImage::open_mapped(self_regions, 0, 64, ImageRole::Interpreter,
                              self_base, page, self) == ElfError::None);
  const ImageRole roles[] = {ImageRole::MainPIE, ImageRole::MainExecutable};
  for (ImageRole role : roles) {
    bool executable = role == ImageRole::MainExecutable;
    Fixture f(role);
    StackFixture input(executable);
    CHECK(decode_initial_stack(input.view(), sp, true, state) ==
          EntryError::None);
    MainLocation location;
    CHECK(locate_main_image(state, f.view().subspan(64, 448), location) ==
          EntryError::None);
    Address v = executable ? base : 0;
    CHECK(location.bias == (executable ? 0 : bias));
    CHECK(location.header_runtime == (executable ? base : bias));
    CHECK(location.header_vaddr == v && location.table_vaddr == v + 64);
    MappedRegion main_regions[] = {
        {v, Bytes(f.bytes, 0x1000)},
        {v + 0x2000, Bytes(f.bytes + 0x2000, 0x100)},
        {v + 0x4000, Bytes(f.bytes + 0x4000, 0x1000)}};
    CHECK(ElfImage::open_mapped(main_regions, v, v + 64, role, location.bias,
                                page, main) == ElfError::None);
    MappedRegion random{sp + 400, Bytes(input.bytes + 400, 16)};
    CHECK(validate_entry_images(state, location, self, main, self_base + 0x2000,
                                random) == EntryError::None);
    CHECK(validate_entry_images(state, location, main, self, self_base + 0x2000,
                                random) == EntryError::Image);
    CHECK(validate_entry_images(state, location, self, main,
                                state.value(AuxField::Entry),
                                random) == EntryError::Entry);
    random.bytes = Bytes(input.bytes + 400, 15);
    CHECK(validate_entry_images(state, location, self, main, self_base + 0x2000,
                                random) == EntryError::Random);
    random.bytes = Bytes(input.bytes + 400, 16);
    state.values[static_cast<unsigned>(AuxField::Entry)] += 4;
    CHECK(validate_entry_images(state, location, self, main, self_base + 0x2000,
                                random) == EntryError::Entry);
    CHECK(decode_initial_stack(input.view(), sp, true, state) ==
          EntryError::None);
    state.values[static_cast<unsigned>(AuxField::Base)] += page;
    CHECK(validate_entry_images(state, location, self, main, self_base + 0x2000,
                                random) == EntryError::Image);
    CHECK(decode_initial_stack(input.view(), sp, true, state) ==
          EntryError::None);
    location.bias += page;
    CHECK(validate_entry_images(state, location, self, main, self_base + 0x2000,
                                random) == EntryError::Image);
    CHECK(locate_main_image(state, f.view().subspan(64, 448), location) ==
          EntryError::None);
    ElfImage file_image;
    CHECK(ElfImage::open_file(f.view(), role, location.bias, page,
                              file_image) == ElfError::None);
    CHECK(validate_entry_images(state, location, self, file_image,
                                self_base + 0x2000,
                                random) == EntryError::Image);
    for (unsigned n = 0; n < 448; ++n)
      CHECK(locate_main_image(state, f.view().subspan(64, n), location) ==
            EntryError::ProgramHeaders);
    f.put(ph(0, 16), 8, state.value(AuxField::Phdr) + page);
    CHECK(locate_main_image(state, f.view().subspan(64, 448), location) ==
          EntryError::ProgramHeaders);
  }
  CHECK(decode_initial_stack(stack.view(), sp, true, state) ==
        EntryError::None);
  Fixture malformed;
  MainLocation rejected;
  malformed.put(ph(7), 4, PT_PHDR);
  CHECK(locate_main_image(state, malformed.view().subspan(64, 448), rejected) ==
        EntryError::ProgramHeaders);
  CHECK(!rejected.valid);
  malformed.put(ph(7), 4, PT_NULL);
  malformed.put(ph(0), 4, PT_NULL);
  CHECK(locate_main_image(state, malformed.view().subspan(64, 448), rejected) ==
        EntryError::ProgramHeaders);
  Fixture overlap;
  MappedRegion overlap_region{0, overlap.view()};
  CHECK(ElfImage::open_mapped({&overlap_region, 1}, 0, 64, ImageRole::MainPIE,
                              self_base, page, main) == ElfError::None);
  StackFixture same;
  same.aux(0, self_base + 64);
  same.aux(5, self_base + 0x2000);
  CHECK(decode_initial_stack(same.view(), sp, true, state) == EntryError::None);
  MainLocation location;
  CHECK(locate_main_image(state, overlap.view().subspan(64, 448), location) ==
        EntryError::None);
  MappedRegion random{sp + 400, Bytes(same.bytes + 400, 16)};
  CHECK(validate_entry_images(state, location, self, main, self_base + 0x2000,
                              random) == EntryError::Image);
  return 0;
}
