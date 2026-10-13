//===-- Native validation of a linked bootstrap fixture -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/dynamic.h"
#include "loader/linux/relocation.h"
#include "loader/linux/self_relocation.h"
#include "loader/linux/symbols.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace LIBC_NAMESPACE::loader;

static void check(bool condition, int line = __builtin_LINE()) {
  if (!condition) {
    std::fprintf(stderr, "linked-image check failed at line %d\n", line);
    std::abort();
  }
}

static Address read_octa(const unsigned char *bytes) {
  Address result = 0;
  for (unsigned i = 0; i < 8; ++i)
    result = (result << 8) | bytes[i];
  return result;
}

int main(int argc, char **argv) {
  check(argc == 2 || (argc == 3 && (argv[2][0] == 's' || argv[2][0] == 'r')));
  bool runtime = argc == 3 && argv[2][0] == 'r';
  ImageRole role = argc == 2 ? ImageRole::Interpreter : ImageRole::SharedObject;
  FILE *input = std::fopen(argv[1], "rb");
  check(input && std::fseek(input, 0, SEEK_END) == 0);
  long size = std::ftell(input);
  check(size > 0 && std::fseek(input, 0, SEEK_SET) == 0);
  std::vector<unsigned char> file(static_cast<size_t>(size));
  check(std::fread(file.data(), 1, file.size(), input) == file.size());
  check(std::fclose(input) == 0);
  constexpr Address bias = 0x1000000, page = 8192;
  ElfImage image;
  check(ElfImage::open_file({file.data(), file.size()}, role, bias, page,
                            image) == ElfError::None);
  const auto header = *image.header();
  DynamicInfo metadata;
  check(DynamicInfo::open(image, metadata) == DynamicError::None);
  check(metadata.symbol_count() >= (argc == 2 ? 1 : 4));
  // The runtime parser must not consult section headers, even when the producer
  // supplies them. Keep the original file size and mapped bytes unchanged.
  for (unsigned i = 40; i < 48; ++i)
    file[i] = 0;
  for (unsigned i = 58; i < 64; ++i)
    file[i] = 0;
  check(ElfImage::open_file({file.data(), file.size()}, role, bias, page,
                            image) == ElfError::None);
  check(DynamicInfo::open(image, metadata) == DynamicError::None);
  auto reject_byte = [&](Bytes view, Address offset,
                         unsigned char replacement) {
    check(offset < view.size());
    // The view refers to our mutable fixture, not production input storage.
    auto *byte = file.data() + (view.data() - file.data()) + offset;
    unsigned char saved = *byte;
    *byte = replacement;
    DynamicInfo rejected;
    check(DynamicInfo::open(image, rejected) != DynamicError::None);
    check(!rejected.symbol_count());
    *byte = saved;
  };
  reject_byte(metadata.sysv_hash(), 3, 0); // Actual fixture has <256 buckets.
  reject_byte(metadata.gnu_hash(), 11, 0); // Empty bloom table.
  reject_byte(image.file_backed_bytes(metadata.value(DynamicTag::Strtab),
                                      metadata.value(DynamicTag::Strsz)),
              0, 1);
  reject_byte(image.file_backed_bytes(metadata.value(DynamicTag::Symtab), 24),
              4, 1);
  reject_byte(image.file_backed_bytes(metadata.value(DynamicTag::Rela),
                                      metadata.value(DynamicTag::Relasz)),
              15, 44);
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
                              role, bias, page, image) == ElfError::None);
  check(DynamicInfo::open(image, metadata) == DynamicError::None);
  if (role == ImageRole::SharedObject) {
    check(metadata.lifecycle_array(DynamicTag::InitArray).size() == 8);
    check(metadata.lifecycle_array(DynamicTag::FiniArray).size() == 8);
    ElfImage second;
    DynamicInfo second_metadata;
    check(ElfImage::open_file({file.data(), file.size()}, role, bias * 2, page,
                              second) == ElfError::None);
    check(DynamicInfo::open(second, second_metadata) == DynamicError::None);
    SymbolObject objects[] = {{&image, &metadata}, {&second, &second_metadata}};
    StartupSymbols symbols;
    check(StartupSymbols::open(objects, symbols) == SymbolError::None);
    auto name = [](const char *s) {
      return Bytes(reinterpret_cast<const unsigned char *>(s), std::strlen(s));
    };
    auto index = [&](const char *s) {
      for (Address i = 1; i < metadata.symbol_count(); ++i) {
        Elf64_Sym symbol;
        Bytes text;
        check(metadata.symbol(i, symbol) &&
              metadata.string(symbol.st_name, text));
        if (text.size() == std::strlen(s) &&
            std::memcmp(text.data(), s, text.size()) == 0)
          return i;
      }
      check(false);
      return Address(0);
    };
    SymbolValue value, pointer;
    check(symbols.lookup(name("alpha"), value) == SymbolError::None);
    check(symbols.resolve(1, index("alpha"), pointer) == SymbolError::None);
    check(value.object == 0 && pointer.address == value.address);
    check(symbols.lookup(name("beta"), value) == SymbolError::None);
    check(symbols.resolve(1, index("beta"), pointer) == SymbolError::None);
    check(value.object == 0 && pointer.object == 1 &&
          pointer.address == value.address + bias);
    check(symbols.lookup(name("weak_hook"), value) == SymbolError::None &&
          value.object == 0);
    check(symbols.lookup(name("hook"), value) == SymbolError::Undefined);
    check(symbols.resolve(1, index("optional"), value) == SymbolError::None &&
          value.weak_zero && value.address == 0);
    check(symbols.resolve(1, index("missing"), value) ==
          (runtime ? SymbolError::None : SymbolError::Undefined));
    SymbolFailure failure;
    if (!runtime)
      check(symbols.validate_references(failure) == SymbolError::Undefined &&
            failure.object == 0 && failure.symbol == index("missing"));
    else
      check(symbols.validate_references(failure) == SymbolError::None);
    SymbolObject relocating[] = {{&image, &metadata}};
    LIBC_NAMESPACE::cpp::span<const WritableRegion> writes[] = {
        {writable.data(), writable.size()}};
    std::vector<Address> expected;
    check(StartupSymbols::open(relocating, symbols) == SymbolError::None);
    bool relative_seen = false, pointer_seen = false, got_seen = false;
    for (Address i = 0; i < metadata.relocation_count(); ++i) {
      DynamicRelocation r;
      check(metadata.relocation(i, r));
      Address resolved = bias;
      if (r.info != 41) {
        auto error = symbols.resolve(0, r.info >> 32, value);
        check(error == SymbolError::None || !runtime);
        resolved = value.address;
      }
      expected.push_back(
          runtime
              ? resolved + r.addend
              : read_octa(image.mapped_memory_bytes(r.offset, 8, PF_R).data()));
      relative_seen |= r.info == 41;
      pointer_seen |= static_cast<Elf64_Word>(r.info) == 5;
      got_seen |= static_cast<Elf64_Word>(r.info) == 42;
    }
    check(relative_seen && pointer_seen && got_seen);
    check(relocate_startup(relocating, writes).error ==
          (runtime ? StartupRelocationError::None
                   : StartupRelocationError::Symbol));
    for (Address i = 0; i < metadata.relocation_count(); ++i) {
      DynamicRelocation r;
      check(metadata.relocation(i, r));
      check(read_octa(image.mapped_memory_bytes(r.offset, 8, PF_R).data()) ==
            expected[i]);
    }
    std::puts(
        runtime
            ? "eager RELATIVE/GLOB_DAT/64 values passed"
            : "missing strong symbol leaves all relocation bytes unchanged");
    std::puts(
        "linked DSO: dynamic file/mapped views and five malformed "
        "mutations, preemption/protected/weak/function address checks passed");
    return 0;
  }
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
  std::puts("linked bootstrap: dynamic views, five malformed mutations and "
            "RELATIVE values passed");
}
