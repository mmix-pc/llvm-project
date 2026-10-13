//===-- Checked loader ELF views ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_ELF_H
#define LLVM_LIBC_LOADER_LINUX_ELF_H

#include "hdr/elf_proxy.h"
#include "src/__support/CPP/span.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

using Bytes = cpp::span<const unsigned char>;
using Address = Elf64_Addr;

enum class ImageRole { MainExecutable, MainPIE, Interpreter, SharedObject };
enum class ElfError {
  None,
  Truncated,
  Header,
  Target,
  AddressRange,
  Load,
  Overlap,
  ProgramHeaders,
  Interpreter,
  Dynamic,
  Stack,
  Relro,
  Entry,
};

// Regions are sorted, disjoint, readable, immutable storage owned by the
// caller. vaddr is a linked address, not a host pointer or a bias-adjusted
// runtime PC.
struct MappedRegion {
  Address vaddr;
  Bytes bytes;
};

// Byte decoders accept either ELF64 byte order and never cast input to structs.
bool decode_elf_header(Bytes bytes, Elf64_Ehdr &header);
bool decode_program_header(Bytes bytes, bool big_endian, Elf64_Phdr &header);

class ElfImage {
  Elf64_Ehdr ehdr;
  Bytes file;
  Bytes phdrs;
  cpp::span<const MappedRegion> regions;
  Address header_vaddr = 0;
  Address table_vaddr = 0;
  Address bias = 0;
  bool mapped = false;
  bool admitted = false;
  ImageRole image_role = ImageRole::SharedObject;

  Bytes mapped_bytes(Address address, Address size) const;
  bool coverage(Address address, Address size, unsigned flags, bool file_backed,
                Address offset, bool check_offset) const;
  bool find_file_address(Address offset, Address size, Address &address) const;
  bool read_program_header(unsigned index, Elf64_Phdr &out) const;
  Bytes read_file_bytes(Address vaddr, Address size) const;
  ElfError validate(ImageRole role, Address page_size);

public:
  ElfImage() { __builtin_memset_inline(&ehdr, 0, sizeof(ehdr)); }
  ElfImage(const ElfImage &other) { *this = other; }
  ElfImage &operator=(const ElfImage &other) {
    if (this == &other)
      return *this;
    // Fixed-size inline copies must not acquire a libc dependency at bootstrap.
    __builtin_memcpy_inline(&ehdr, &other.ehdr, sizeof(ehdr));
    file = other.file;
    phdrs = other.phdrs;
    regions = other.regions;
    header_vaddr = other.header_vaddr;
    table_vaddr = other.table_vaddr;
    bias = other.bias;
    mapped = other.mapped;
    admitted = other.admitted;
    image_role = other.image_role;
    return *this;
  }
  // A failed open invalidates out. The supplied storage must remain unchanged
  // and alive for the lifetime of the admitted view. No allocations are
  // performed.
  static ElfError open_file(Bytes file, ImageRole role, Address bias,
                            Address page_size, ElfImage &out);
  // Caller establishes readability of every region before entry. This cannot
  // recover from an invalid host pointer or prove kernel file provenance.
  // File-length checks belong to open_file; mapped views recheck metadata and
  // readable spans without pretending that a virtual hole is file storage.
  static ElfError open_mapped(cpp::span<const MappedRegion> regions,
                              Address header_vaddr, Address table_vaddr,
                              ImageRole role, Address bias, Address page_size,
                              ElfImage &out);
  const Elf64_Ehdr *header() const { return admitted ? &ehdr : nullptr; }
  bool is_mapped() const { return admitted && mapped; }
  ImageRole role() const { return image_role; }
  Address load_bias() const { return bias; }
  Address header_address() const { return header_vaddr; }
  Address table_address() const { return table_vaddr; }
  bool program_header(unsigned index, Elf64_Phdr &out) const {
    return admitted && read_program_header(index, out);
  }
  // Only file-backed readable bytes are returned, never zero-fill BSS.
  Bytes file_backed_bytes(Address vaddr, Address size) const {
    return admitted ? read_file_bytes(vaddr, size) : Bytes();
  }
  bool runtime_address(Address vaddr, Address size, unsigned flags,
                       Address &out) const;
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_ELF_H
