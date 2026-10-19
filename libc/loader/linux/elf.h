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

// Input bounds belong to the caller. Aligned words can use native loads even
// when the ELF byte order differs; unaligned input never requires such a load.
template <typename T>
inline T read_elf_integer(const unsigned char *data, bool big) {
  static_assert(sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);
  T value;
  if (!(reinterpret_cast<uintptr_t>(data) % alignof(T))) {
    __builtin_memcpy_inline(&value,
                            __builtin_assume_aligned(data, alignof(T)),
                            sizeof(T));
    if (big != (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)) {
      if constexpr (sizeof(T) == 2)
        value = __builtin_bswap16(value);
      else if constexpr (sizeof(T) == 4)
        value = __builtin_bswap32(value);
      else
        value = __builtin_bswap64(value);
    }
    return value;
  }
  value = 0;
  for (unsigned i = 0; i < sizeof(T); ++i)
    value = static_cast<T>((value << 8) |
                          data[big ? i : sizeof(T) - 1 - i]);
  return value;
}

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
  TLS,
};

// Regions are sorted, disjoint, readable storage owned by the caller. ELF
// metadata remains immutable; relocation may modify admitted writable data.
// vaddr is a linked address, not a host pointer or a bias-adjusted runtime PC.
struct MappedRegion {
  Address vaddr;
  Bytes bytes;
};

// Copy aligned native-order records without aliasing input as a C++ object.
// Callers establish the byte extent; other inputs use the byte decoders.
template <typename T>
inline bool copy_native_record(const unsigned char *data, bool big, T &out) {
  static_assert(__is_trivially_copyable(T));
  if (big != (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) ||
      reinterpret_cast<uintptr_t>(data) % alignof(T))
    return false;
  __builtin_memcpy_inline(&out, __builtin_assume_aligned(data, alignof(T)),
                          sizeof(T));
  return true;
}

// Decoders accept either ELF64 byte order and arbitrary input alignment.
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
  bool tls_present = false;
  Elf64_Phdr tls;
  ImageRole image_role = ImageRole::SharedObject;

  // Most images have only a few LOADs. This is an allocation-free query cache,
  // not an admission limit; larger layouts retain the decoded-table path.
  struct LoadRange {
    Address address, file_size, memory_size, offset;
    unsigned flags;
  };
  static constexpr unsigned LOAD_CACHE_SIZE = 4;
  LoadRange load_cache[LOAD_CACHE_SIZE];
  unsigned load_count = 0;
  bool load_cache_ready = false;

  Bytes mapped_bytes(Address address, Address size) const;
  bool coverage(Address address, Address size, unsigned flags, bool file_backed,
                Address offset, bool check_offset) const;
  bool find_file_address(Address offset, Address size, Address &address) const;
  bool read_program_header(unsigned index, Elf64_Phdr &out) const;
  Bytes read_file_bytes(Address vaddr, Address size) const;
  ElfError validate(ImageRole role, Address page_size);

public:
  ElfImage() { __builtin_memset_inline(&ehdr, 0, sizeof(ehdr)); }
  // Drop borrowed storage without clearing unused decoded header bytes.
  void reset() {
    file = {};
    phdrs = {};
    regions = {};
    header_vaddr = table_vaddr = bias = 0;
    mapped = admitted = tls_present = false;
    load_count = 0;
    load_cache_ready = false;
  }
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
    tls_present = other.tls_present;
    if (tls_present)
      __builtin_memcpy_inline(&tls, &other.tls, sizeof(tls));
    image_role = other.image_role;
    load_count = other.load_count;
    load_cache_ready = other.load_cache_ready;
    if (load_cache_ready)
      __builtin_memcpy_inline(load_cache, other.load_cache, sizeof(load_cache));
    return *this;
  }
  // A failed open invalidates out. ELF metadata must remain unchanged and all
  // storage alive for the lifetime of the admitted view. No allocations are
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
  // Recheck borrowed layout before a whole-image protection transition.
  ElfError revalidate_layout(Address page_size) const {
    if (!admitted)
      return ElfError::Header;
    ElfImage copy = *this;
    return copy.validate(image_role, page_size);
  }
  ImageRole role() const { return image_role; }
  const Elf64_Phdr *tls_header() const {
    return admitted && tls_present ? &tls : nullptr;
  }
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
  // Bound tables without explicit sizes (hash chains and dynamic symbols) by
  // contiguous readable file-backed storage, never a section header or BSS.
  Bytes file_backed_tail(Address vaddr) const;
  Bytes mapped_memory_bytes(Address vaddr, Address size, unsigned flags) const {
    return admitted && mapped && coverage(vaddr, size, flags, false, 0, false)
               ? mapped_bytes(vaddr, size)
               : Bytes();
  }
  bool runtime_address(Address vaddr, Address size, unsigned flags,
                       Address &out) const;
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_ELF_H
