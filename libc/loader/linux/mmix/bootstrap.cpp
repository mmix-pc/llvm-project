//===-- MMIX interpreter bootstrap ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/arena.h"
#include "loader/linux/entry.h"
#include "loader/linux/io.h"
#include "loader/linux/protection.h"
#include "loader/linux/self_relocation.h"
#include "loader/linux/target.h"
#include "src/__support/CPP/new.h"

using namespace LIBC_NAMESPACE::loader;

extern "C" {
extern const unsigned char __ehdr_start[] __attribute__((visibility("hidden")));
// Borrowed startup views remain alive until this ordinary hidden continuation
// returns. It must adopt persistent state before returning the main entry PC.
__attribute__((visibility("hidden"))) Address
__llvm_libc_loader_after_relocation(const InitialStack *, const ElfImage *, Arena *);
}

// The kernel supplies a valid readable initial stack. Check address arithmetic
// before each read; this is not recovery from a corrupt kernel mapping.
static Address stack_word(Address &cursor) {
  if (!is_user_range(cursor, 8))
    fatal_startup();
  Address value = *reinterpret_cast<const Address *>(cursor);
  cursor += 8;
  return value;
}

static Bytes initial_stack(Address sp) {
  if (sp & 7)
    fatal_startup();
  Address cursor = sp;
  Address argc = stack_word(cursor);
  if (argc > __INT_MAX__ || !is_user_range(cursor, (argc + 1) * 8))
    fatal_startup();
  for (Address i = 0; i < argc; ++i)
    (void)stack_word(cursor);
  if (stack_word(cursor) != 0)
    fatal_startup();
  while (stack_word(cursor) != 0) {
  }
  for (;;) {
    Address tag = stack_word(cursor);
    Address value = stack_word(cursor);
    if (tag == AT_NULL) {
      if (value)
        fatal_startup();
      break;
    }
  }
  return Bytes(reinterpret_cast<const unsigned char *>(sp), cursor - sp);
}

extern "C" __attribute__((visibility("hidden"))) Address
__llvm_libc_loader_bootstrap(Address sp, Address entry_pc) {
  InitialStack stack;
  if (decode_initial_stack(initial_stack(sp), sp, true, stack) !=
      EntryError::None)
    fatal_startup();
  Address base = stack.value(AuxField::Base);
  Address page = stack.value(AuxField::Pagesz);
  // The linked interpreter's first readable page owns both ELF header and
  // program table. The artifact gate checks this bootstrap-specific layout.
  // Use a hidden PC-relative linker symbol, never an unrelocated global
  // pointer.
  if (base != reinterpret_cast<Address>(__ehdr_start) ||
      !is_user_range(base, page))
    fatal_startup();
  Bytes first_page(__ehdr_start, page);
  Elf64_Ehdr header;
  if (!decode_elf_header(first_page, header) || !is_supported_target(header) ||
      header.e_phoff > page ||
      Address(header.e_phnum) * 56 > page - header.e_phoff)
    fatal_startup();
  Bytes phdrs =
      first_page.subspan(header.e_phoff, Address(header.e_phnum) * 56);
  Arena arena(page);
  void *storage;
  if (arena.allocate(header.e_phnum * sizeof(MappedRegion),
                     alignof(MappedRegion), storage))
    fatal_startup();
  auto *regions = static_cast<MappedRegion *>(storage);
  if (arena.allocate(header.e_phnum * sizeof(WritableRegion),
                     alignof(WritableRegion), storage))
    fatal_startup();
  auto *writable = static_cast<WritableRegion *>(storage);
  unsigned count = 0, writes = 0;
  for (unsigned i = 0; i < header.e_phnum; ++i) {
    Elf64_Phdr p;
    if (!decode_program_header(phdrs.subspan(Address(i) * 56, 56), true, p))
      fatal_startup();
    if (p.p_type != PT_LOAD || !p.p_memsz)
      continue;
    Address runtime;
    if (!(p.p_flags & PF_R) ||
        __builtin_add_overflow(base, p.p_vaddr, &runtime) ||
        !is_user_range(runtime, p.p_memsz))
      fatal_startup();
    auto *bytes = reinterpret_cast<unsigned char *>(runtime);
    new (regions + count++) MappedRegion{p.p_vaddr, Bytes(bytes, p.p_memsz)};
    if (p.p_flags & PF_W)
      new (writable + writes++) WritableRegion{p.p_vaddr, {bytes, p.p_memsz}};
  }
  ElfImage self;
  if (ElfImage::open_mapped({regions, count}, 0, header.e_phoff,
                            ImageRole::Interpreter, base, page,
                            self) != ElfError::None)
    fatal_startup();
  Address actual_entry;
  if (!self.runtime_address(header.e_entry, 4, PF_X, actual_entry) ||
      actual_entry != entry_pc ||
      relocate_self(self, {writable, writes}) != RelocationError::None)
    fatal_startup();
  const ElfImage *images[] = {&self};
  if (finalize_image_protections(images, page))
    fatal_startup();
  Address entry = __llvm_libc_loader_after_relocation(&stack, &self, &arena);
  if (entry != stack.value(AuxField::Entry))
    fatal_startup();
  return entry;
}
