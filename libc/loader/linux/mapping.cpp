//===-- Transactional loader DSO mappings ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/mapping.h"
#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "hdr/sys_mman_macros.h"
#include "loader/linux/syscall.h"
#include "loader/linux/target.h"
#include "src/__support/CPP/new.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace loader {

static bool add(Address a, Address b, Address &out) {
  out = a + b;
  return out >= a;
}

static bool round_up(Address value, Address alignment, Address &out) {
  if (!add(value, alignment - 1, out))
    return false;
  out &= ~(alignment - 1);
  return true;
}

long DsoMapping::Range::release() {
  if (!size)
    return 0;
  long error = raw_syscall(SYS_munmap, address, size);
  if (error >= 0) {
    address = 0;
    size = 0;
  }
  return error < 0 ? error : 0;
}

long DsoMapping::release_image() {
  ready = false;
  long error = reservation.release();
  long storage_error = arena.reset();
  arena_owned = storage_error != 0;
  return error ? error : storage_error;
}

long DsoMapping::reset() {
  ready = false;
  long error = fd.close();
  long staging_error = staging.release();
  long image_error = release_image();
  return error ? error : staging_error ? staging_error : image_error;
}

bool DsoMapping::busy() const {
  return ready || fd.get() >= 0 || staging.size || reservation.size || arena_owned;
}

MapResult DsoMapping::open(const char *path) {
  if (busy())
    return {-EBUSY, 0};
  if (!is_supported_page_size(page_size))
    return {-EINVAL, 0};
  long error = fd.open_readonly(path);
  return error ? MapResult{error, 0} : finish_open();
}

MapResult DsoMapping::open(OwnedFd &source) {
  if (busy())
    return {-EBUSY, 0};
  if (!is_supported_page_size(page_size))
    return {-EINVAL, 0};
  long error = fd.take(source);
  return error ? MapResult{error, 0} : finish_open();
}

MapResult DsoMapping::finish_open() {
  long error = populate();
  if (error)
    return {error, reset()};
  // Consume the fd even on close failure. Do not immediately retry a failed
  // staging unmap; retain it for the caller's explicit reset instead.
  long close_error = fd.close();
  long staging_error = staging.release();
  if (close_error || staging_error) {
    long cleanup_error = release_image();
    return {close_error ? close_error : staging_error,
            staging_error ? staging_error : cleanup_error};
  }
  ready = true;
  return {0, 0};
}

long DsoMapping::replace(Address address, Address size, int prot,
                         bool anonymous, Address offset) {
  if (address < reservation.address || size > reservation.size ||
      address - reservation.address > reservation.size - size)
    return -ENOEXEC;
  long result =
      raw_syscall(SYS_mmap, address, size, prot,
                  MAP_PRIVATE | MAP_FIXED | (anonymous ? MAP_ANONYMOUS : 0),
                  anonymous ? -1 : fd.get(), offset);
  return result < 0 ? result : 0;
}

long DsoMapping::map_segment(const Elf64_Phdr &p, Address bias) {
  Address start = p.p_vaddr & ~(page_size - 1);
  Address end, file_end;
  // Preflight has checked all additions and page rounding.
  round_up(p.p_vaddr + p.p_memsz, page_size, end);
  round_up(p.p_vaddr + p.p_filesz, page_size, file_end);
  int prot = PROT_READ | ((p.p_flags & PF_W) ? PROT_WRITE : 0) |
             ((p.p_flags & PF_X) ? PROT_EXEC : 0);
  if (p.p_filesz) {
    Address zero_start = p.p_vaddr + p.p_filesz;
    Address zero_end = p.p_vaddr + p.p_memsz;
    if (zero_end > file_end)
      zero_end = file_end;
    bool zero = zero_start < zero_end;
    // Never create W+X pages, including temporary partial-page BSS writes.
    long error = replace(bias + start, file_end - start,
                         zero ? PROT_READ | PROT_WRITE : prot, false,
                         p.p_offset & ~(page_size - 1));
    if (error)
      return error;
    if (zero) {
      auto *bytes =
          reinterpret_cast<volatile unsigned char *>(bias + zero_start);
      for (Address i = 0; i < zero_end - zero_start; ++i)
        bytes[i] = 0;
      if (prot != (PROT_READ | PROT_WRITE)) {
        error = raw_syscall(SYS_mprotect, bias + start, file_end - start, prot);
        if (error < 0)
          return error;
      }
    }
    start = file_end;
  }
  return start < end ? replace(bias + start, end - start, prot, true, 0) : 0;
}

long DsoMapping::populate() {
  long length = raw_syscall(SYS_lseek, fd.get(), 0, SEEK_END);
  if (length < 0)
    return length;
  if (length < 64)
    return -ENOEXEC;
  Address stage_size;
  if (!round_up(length, page_size, stage_size) || stage_size > __LONG_MAX__)
    return -EOVERFLOW;
  // Borrow a read-only file view for preflight instead of copying the entire
  // DSO, including code and debug sections that validation never reads. The
  // input-stability requirement is the same as for the final file mappings.
  long result = raw_syscall(SYS_mmap, 0, stage_size, PROT_READ, MAP_PRIVATE,
                            fd.get(), 0);
  if (result < 0)
    return result;
  staging.address = result;
  staging.size = stage_size;
  auto *data = reinterpret_cast<unsigned char *>(result);
  Bytes file(data, length);
  Elf64_Ehdr header;
  if (!decode_elf_header(file, header) || header.e_phoff > file.size() ||
      Address(header.e_phnum) * 56 > file.size() - header.e_phoff)
    return -ENOEXEC;
  bool big = header.e_ident[EI_DATA] == ELFDATA2MSB;
  Address first = ~Address(0), last = 0, alignment = page_size;
  unsigned count = 0;
  for (unsigned i = 0; i < header.e_phnum; ++i) {
    Elf64_Phdr p;
    decode_program_header(Bytes(data + header.e_phoff + i * 56, 56), big, p);
    if (p.p_type != PT_LOAD)
      continue;
    if (p.p_align > alignment) {
      if ((p.p_align & (p.p_align - 1)) || p.p_align > __LONG_MAX__)
        return -ENOEXEC;
      alignment = p.p_align;
    }
    if (!p.p_memsz)
      continue;
    Address end;
    if (!add(p.p_vaddr, p.p_memsz, end) || !round_up(end, page_size, end))
      return -ENOEXEC;
    Address start = p.p_vaddr & ~(page_size - 1);
    if (start < first)
      first = start;
    if (end > last)
      last = end;
    ++count;
  }
  ElfImage input;
  if (!count ||
      ElfImage::open_file(file, ImageRole::SharedObject,
                          first < page_size ? alignment : 0, page_size,
                          input) != ElfError::None)
    return -ENOEXEC;

  Address size;
  if (!add(last - first, alignment - page_size, size) || size > __LONG_MAX__)
    return -EOVERFLOW;
  result = raw_syscall(SYS_mmap, first < page_size ? page_size : first, size,
                       PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (result < 0)
    return result;
  reservation.address = result;
  reservation.size = size;
  Address bias;
  if (reservation.address < first ||
      !is_user_range(reservation.address, size) ||
      (reservation.address & (page_size - 1)) ||
      !round_up(reservation.address - first, alignment, bias) ||
      bias + first < reservation.address ||
      bias + last > reservation.address + size)
    return -ENOEXEC;
  // Recheck rebased LOAD ranges. Dynamic metadata is fully validated on the
  // final mapping below, before publication, relocation or execution.
  if (ElfImage::open_file(file, ImageRole::SharedObject, bias, page_size,
                          input) != ElfError::None)
    return -ENOEXEC;
  void *storage;
  long error = arena.allocate(count * sizeof(MappedRegion),
                             alignof(MappedRegion), storage);
  if (error)
    return error;
  arena_owned = true;
  auto *regions = static_cast<MappedRegion *>(storage);
  unsigned index = 0;
  for (unsigned i = 0; i < header.e_phnum; ++i) {
    Elf64_Phdr p;
    input.program_header(i, p);
    if (p.p_type != PT_LOAD || !p.p_memsz)
      continue;
    error = map_segment(p, bias);
    if (error)
      return error;
    new (regions + index++) MappedRegion{
        p.p_vaddr,
        Bytes(reinterpret_cast<const unsigned char *>(bias + p.p_vaddr),
              p.p_memsz)};
  }
  if (ElfImage::open_mapped(cpp::span<const MappedRegion>(regions, count),
                            input.header_address(), input.table_address(),
                            ImageRole::SharedObject, bias, page_size,
                            mapped) != ElfError::None ||
      DynamicInfo::open(mapped, dynamic) != DynamicError::None)
    return -ENOEXEC;
  return 0;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
