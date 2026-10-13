//===-- Loader ELF input validation tests ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "elf_fixture.h"
#include "loader/linux/target.h"
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

extern "C" int main() {
  ElfImage image;
  const ImageRole roles[] = {ImageRole::MainExecutable, ImageRole::MainPIE,
                             ImageRole::Interpreter, ImageRole::SharedObject};
  for (ImageRole role : roles) {
    Fixture f(role);
    Address v = role == ImageRole::MainExecutable ? base : 0;
    Address b = role == ImageRole::MainExecutable ? 0 : bias;
    CHECK(ElfImage::open_file(f.view(), role, b, page, image) ==
          ElfError::None);
    CHECK(image.header()->e_shnum == 0);
    Address runtime;
    CHECK(image.runtime_address(v + 0x2000, 4, PF_X, runtime));
    CHECK(runtime == b + v + 0x2000);
    CHECK(!image.runtime_address(v + 0x4000, 4, PF_X, runtime));
    CHECK(image.file_backed_bytes(v + 0x5000, 1).empty()); // BSS is not file.
    CHECK(image.file_backed_bytes(v + 0x3000, 1).empty()); // Nor a load hole.
    Elf64_Phdr p;
    CHECK(!image.program_header(8, p));
    MappedRegion regions[] = {{v, Bytes(f.bytes, 0x1000)},
                              {v + 0x2000, Bytes(f.bytes + 0x2000, 0x100)},
                              {v + 0x4000, Bytes(f.bytes + 0x4000, 0x1000)}};
    CHECK(ElfImage::open_mapped(regions, v, v + 64, role, b, page, image) ==
          ElfError::None);
    CHECK(image.file_backed_bytes(v + 0x4000, 32).data() == f.bytes + 0x4000);
    regions[2].bytes = Bytes(f.bytes + 0x4000, 16);
    CHECK(ElfImage::open_mapped(regions, v, v + 64, role, b, page, image) ==
          ElfError::Dynamic);
    CHECK(!image.runtime_address(v + 0x2000, 4, PF_X, runtime));
    CHECK(!image.header());
    CHECK(!image.program_header(0, p));
    CHECK(image.file_backed_bytes(v, 64).empty());
  }

  Fixture f;
  for (unsigned n = 0; n < 64; ++n) {
    Elf64_Ehdr h;
    CHECK(!decode_elf_header(Bytes(f.bytes, n), h));
  }
  for (unsigned n = 64; n < 512; ++n)
    CHECK(ElfImage::open_file(Bytes(f.bytes, n), ImageRole::MainPIE, bias, page,
                              image) == ElfError::Truncated);
  CHECK(ElfImage::open_file(Bytes(f.bytes, 0x4fff), ImageRole::MainPIE, bias,
                            page, image) == ElfError::Load);
  Fixture little(ImageRole::MainPIE, false);
  Elf64_Ehdr h;
  Elf64_Phdr p;
  CHECK(decode_elf_header(little.view(), h));
  CHECK(h.e_machine == EM_MMIX && h.e_entry == 0x2000 && h.e_phoff == 64);
  CHECK(decode_program_header(little.view().subspan(ph(3), 56), false, p));
  CHECK(p.p_vaddr == 0x4000 && p.p_filesz == 0x1000 && p.p_memsz == page);
  CHECK(!decode_program_header(little.view().subspan(ph(3), 55), false, p));
  CHECK(ElfImage::open_file(little.view(), ImageRole::MainPIE, bias, page,
                            image) == ElfError::Target);

  struct Mutation {
    unsigned offset, width;
    Address value;
    ElfError error;
  };
  const Mutation bad[] = {
      {0, 1, 0, ElfError::Header},
      {EI_CLASS, 1, ELFCLASS32, ElfError::Header},
      {EI_DATA, 1, 0, ElfError::Header},
      {EI_VERSION, 1, 0, ElfError::Header},
      {20, 4, 0, ElfError::Header},
      {52, 2, 63, ElfError::Header},
      {54, 2, 55, ElfError::Header},
      {56, 2, 0, ElfError::Header},
      {56, 2, PN_XNUM, ElfError::Header},
      {32, 8, ~Address(0) - 32, ElfError::Header},
      {32, 8, 0, ElfError::Header},
      {18, 2, EM_X86_64, ElfError::Target},
      {EI_OSABI, 1, 255, ElfError::Target},
      {EI_ABIVERSION, 1, 1, ElfError::Target},
      {48, 4, 1, ElfError::Target},
      {16, 2, ET_EXEC, ElfError::AddressRange},
      {ph(1, 32), 8, page + 1, ElfError::Load},
      {ph(1, 8), 8, ~Address(0), ElfError::Load},
      {ph(1, 16), 8, ~Address(0), ElfError::Load},
      {ph(1, 16), 8, ~Address(0) - page, ElfError::Load},
      {ph(1, 4), 4, PF_R | PF_W | PF_X, ElfError::Load},
      {ph(1, 4), 4, PF_X, ElfError::Load},
      {ph(1, 4), 4, 0x100 | PF_R, ElfError::Load},
      {ph(1, 8), 8, 1, ElfError::Load},
      {ph(1, 48), 8, 3, ElfError::Load},
      {ph(2, 16), 8, 0, ElfError::Overlap},
      {ph(1, 40), 8, page + 1, ElfError::Overlap},
      {ph(3, 16), 8, Address(1) << 43, ElfError::AddressRange},
      {ph(6), 4, PT_NULL, ElfError::Stack},
      {ph(7), 4, PT_GNU_STACK, ElfError::Stack},
      {ph(6, 4), 4, PF_R | PF_W | PF_X, ElfError::Stack},
      {ph(0), 4, PT_NULL, ElfError::ProgramHeaders},
      {ph(7), 4, PT_PHDR, ElfError::ProgramHeaders},
      {ph(0, 8), 8, 65, ElfError::ProgramHeaders},
      {ph(0, 16), 8, 0x2000, ElfError::ProgramHeaders},
      {ph(0, 32), 8, 447, ElfError::ProgramHeaders},
      {ph(0, 40), 8, 447, ElfError::ProgramHeaders},
      {ph(1, 32), 8, 128, ElfError::ProgramHeaders},
      {ph(4), 4, PT_NULL, ElfError::Interpreter},
      {ph(7), 4, PT_INTERP, ElfError::Interpreter},
      {ph(4, 8), 8, 0x301, ElfError::Interpreter},
      {ph(4, 32), 8, 22, ElfError::Interpreter},
      {ph(4, 16), 8, 0x5000, ElfError::Interpreter},
      {0x300, 1, '.', ElfError::Interpreter},
      {0x305, 1, 0, ElfError::Interpreter},
      {0x314, 1, 'x', ElfError::Interpreter},
      {ph(5), 4, PT_NULL, ElfError::Dynamic},
      {ph(7), 4, PT_DYNAMIC, ElfError::Dynamic},
      {ph(5, 32), 8, 31, ElfError::Dynamic},
      {ph(5, 32), 8, 48, ElfError::Dynamic},
      {ph(5, 16), 8, 0x5000, ElfError::Dynamic},
      {ph(5, 8), 8, 0, ElfError::Dynamic},
      {0x4010, 8, DT_NEEDED, ElfError::Dynamic},
      {ph(7, 16), 8, 0x4001, ElfError::Relro},
      {ph(7, 40), 8, page - 1, ElfError::Relro},
      {ph(7, 16), 8, ~Address(0), ElfError::Relro},
      {ph(7, 16), 8, 0x2000, ElfError::Relro},
      {24, 8, 0x2001, ElfError::Entry},
      {24, 8, 0x4000, ElfError::Entry},
      {24, 8, ~Address(0), ElfError::Entry},
  };
  for (const auto &m : bad) {
    Fixture input;
    input.put(m.offset, m.width, m.value);
    ElfError result = ElfImage::open_file(input.view(), ImageRole::MainPIE,
                                          bias, page, image);
    if (result != m.error)
      fprintf(stderr, "mutation %u: expected %u, got %u\n", m.offset,
              unsigned(m.error), unsigned(result));
    CHECK(result == m.error);
  }

  CHECK(ElfImage::open_file(f.view(), ImageRole::MainPIE, bias, 4096, image) ==
        ElfError::Target);
  CHECK(ElfImage::open_file(f.view(), ImageRole::MainPIE, bias + 1, page,
                            image) == ElfError::AddressRange);
  CHECK(ElfImage::open_file(f.view(), ImageRole::MainPIE, 0, page, image) ==
        ElfError::AddressRange);
  CHECK(ElfImage::open_file(f.view(), ImageRole::MainPIE, ~Address(page - 1),
                            page, image) == ElfError::AddressRange);
  const Address alignments[] = {0, 1, page * 2};
  for (Address align : alignments) {
    Fixture input;
    input.put(ph(1, 48), 8, align);
    CHECK(ElfImage::open_file(input.view(), ImageRole::MainPIE, bias, page,
                              image) == ElfError::None);
  }
  Fixture interpreter(ImageRole::Interpreter);
  interpreter.put(ph(1, 48), 8, page * 2);
  CHECK(ElfImage::open_file(interpreter.view(), ImageRole::Interpreter, bias,
                            page, image) == ElfError::Load);
  interpreter.put(ph(1, 48), 8, page);
  interpreter.put(0x4000, 8, DT_NEEDED);
  CHECK(ElfImage::open_file(interpreter.view(), ImageRole::Interpreter, bias,
                            page, image) == ElfError::Dynamic);
  Fixture shifted(ImageRole::Interpreter);
  for (unsigned i = 0; i < 8; ++i) {
    CHECK(decode_program_header(shifted.view().subspan(ph(i), 56), true, p));
    if (p.p_type != PT_NULL && p.p_type != PT_GNU_STACK)
      shifted.put(ph(i, 16), 8, p.p_vaddr + page);
  }
  shifted.put(24, 8, 0x4000);
  CHECK(ElfImage::open_file(shifted.view(), ImageRole::Interpreter, bias, page,
                            image) == ElfError::ProgramHeaders);
  Fixture executable(ImageRole::MainExecutable);
  CHECK(ElfImage::open_file(executable.view(), ImageRole::MainExecutable, bias,
                            page, image) == ElfError::AddressRange);
  Fixture over_aligned;
  over_aligned.put(ph(1, 48), 8, 2 * page);
  CHECK(ElfImage::open_file(over_aligned.view(), ImageRole::MainPIE,
                            bias + page, page, image) == ElfError::Load);
  Fixture empty_relro;
  empty_relro.put(ph(7, 40), 8, 0);
  CHECK(ElfImage::open_file(empty_relro.view(), ImageRole::MainPIE, bias, page,
                            image) == ElfError::None);
  Fixture alias;
  alias.segment(7, PT_LOAD, PF_R, 0, 0x6000, 0x1000, page, page);
  alias.put(ph(0, 16), 8, 0x6040);
  CHECK(ElfImage::open_file(alias.view(), ImageRole::MainPIE, bias, page,
                            image) == ElfError::None);
  Fixture dso(ImageRole::SharedObject);
  dso.put(ph(0), 4, PT_NULL);
  dso.put(ph(7), 4, PT_NULL);
  // Section fields, even nonsensical ones, are never consulted for loading.
  dso.put(40, 8, ~Address(0));
  dso.put(60, 2, 999);
  CHECK(ElfImage::open_file(dso.view(), ImageRole::SharedObject, bias, page,
                            image) == ElfError::None);
  dso.put(ph(4), 4, PT_INTERP);
  CHECK(ElfImage::open_file(dso.view(), ImageRole::SharedObject, bias, page,
                            image) == ElfError::Interpreter);

  MappedRegion region{0, f.view()};
  CHECK(ElfImage::open_mapped({&region, 1}, 0, 65, ImageRole::MainPIE, bias,
                              page, image) != ElfError::None);
  region.bytes = Bytes(f.bytes, 128);
  CHECK(ElfImage::open_mapped({&region, 1}, 0, 64, ImageRole::MainPIE, bias,
                              page, image) == ElfError::Truncated);
  region.vaddr = ~Address(0) - 32;
  CHECK(ElfImage::open_mapped({&region, 1}, 0, 64, ImageRole::MainPIE, bias,
                              page, image) == ElfError::AddressRange);
  CHECK(!is_user_range((Address(1) << 43) - 4, 8));
  CHECK(is_user_range((Address(1) << 43) - 4, 4));
  MappedRegion overlapping[] = {{0, f.view()}, {0x4000, f.view()}};
  CHECK(ElfImage::open_mapped(overlapping, 0, 64, ImageRole::MainPIE, bias,
                              page, image) == ElfError::AddressRange);
  unsigned char unaligned[sizeof(f.bytes) + 1];
  for (unsigned i = 0; i < sizeof(f.bytes); ++i)
    unaligned[i + 1] = f.bytes[i];
  CHECK(ElfImage::open_file(Bytes(unaligned + 1, sizeof(f.bytes)),
                            ImageRole::MainPIE, bias, page,
                            image) == ElfError::None);
  // Deterministic byte mutations also run under native address/UB sanitizers.
  for (unsigned at = 0; at < 512; ++at) {
    unsigned char saved = f.bytes[at];
    f.bytes[at] ^= 0xff;
    (void)ElfImage::open_file(f.view(), ImageRole::MainPIE, bias, page, image);
    f.bytes[at] = saved;
  }
  return 0;
}
