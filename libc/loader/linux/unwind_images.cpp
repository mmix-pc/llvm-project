//===-- Resident startup unwind images ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/unwind_images.h"
#include "hdr/errno_macros.h"
#include "loader/linux/io.h"
#include "src/__support/CPP/new.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {
static Address add_signed_word(Address base, Elf64_Word raw) {
  // The checked index base is a user address. A 32-bit displacement cannot
  // overflow it upwards, and an underflow lands outside the user range.
  return base + static_cast<Address>(static_cast<int64_t>(
                    static_cast<int32_t>(raw)));
}

static bool add_signed(Address base, Address raw, unsigned width,
                       Address &out) {
  if (width == 4) {
    out = add_signed_word(base, static_cast<Elf64_Word>(raw));
    return true;
  }
  if (raw >> 63) {
    Address magnitude = ~raw + 1;
    if (magnitude > base)
      return false;
    out = base - magnitude;
  } else {
    if (raw > ~Address(0) - base)
      return false;
    out = base + raw;
  }
  return true;
}

static Address word(Bytes bytes, size_t at, unsigned width, bool big_endian) {
  return width == 4 ? read_elf_integer<Elf64_Word>(bytes.data() + at, big_endian)
                    : read_elf_integer<Address>(bytes.data() + at, big_endian);
}

template <class ReadEntry>
static bool valid_entries(const ElfImage &image, Address count,
                          ReadEntry read_entry) {
  Address previous = 0;
  Address pc_first = 1, pc_last = 0, fde_first = 1, fde_last = 0;
  for (Address i = 0; i < count; ++i) {
    Address pc, fde;
    if (!read_entry(pc, fde) ||
        ((pc < pc_first || pc > pc_last) &&
         !image.contains_file_backed_runtime(pc, 1, PF_R | PF_X, pc_first,
                                             pc_last)) ||
        ((fde < fde_first || fde > fde_last) &&
         !image.contains_file_backed_runtime(fde, 4, PF_R, fde_first,
                                             fde_last)) ||
        (i && pc < previous))
      return false;
    previous = pc;
  }
  return true;
}

static bool valid_index(const ElfImage &image, const Elf64_Phdr &header) {
  Bytes bytes = image.file_backed_bytes(header.p_vaddr, header.p_filesz);
  if (bytes.size() < 12 || bytes[0] != 1 ||
      (bytes[1] != 0x1b && bytes[1] != 0x1c) ||
      (bytes[2] != 3 && bytes[2] != 4) ||
      (bytes[3] != 0x3b && bytes[3] != 0x3c))
    return false;
  unsigned pointer_width = bytes[1] == 0x1b ? 4 : 8;
  bool big_endian = image.header()->e_ident[EI_DATA] == ELFDATA2MSB;
  unsigned count_width = bytes[2] == 3 ? 4 : 8;
  unsigned table_width = bytes[3] == 0x3b ? 4 : 8;
  size_t offset = 4 + pointer_width + count_width;
  if (offset > bytes.size())
    return false;
  Address base, frame;
  if (!image.runtime_address(header.p_vaddr, header.p_filesz, PF_R, base) ||
      !add_signed(base + 4, word(bytes, 4, pointer_width, big_endian),
                  pointer_width, frame) ||
      !image.contains_file_backed_runtime(frame, 4, PF_R))
    return false;
  Address count = word(bytes, 4 + pointer_width, count_width, big_endian);
  if (count > (bytes.size() - offset) / (2 * table_width))
    return false;
  const auto *cursor = bytes.data() + offset;
  if (table_width == 4 && big_endian == (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) &&
      reinterpret_cast<uintptr_t>(cursor) % alignof(Elf64_Word) == 0) {
    return valid_entries(image, count, [&](Address &pc, Address &fde) {
      const auto *entry = static_cast<const unsigned char *>(
          __builtin_assume_aligned(cursor, alignof(Elf64_Word)));
      Elf64_Word relative_pc, relative_fde;
      __builtin_memcpy_inline(&relative_pc, entry, sizeof(relative_pc));
      __builtin_memcpy_inline(&relative_fde, entry + 4, sizeof(relative_fde));
      pc = add_signed_word(base, relative_pc);
      fde = add_signed_word(base, relative_fde);
      cursor += 8;
      return true;
    });
  }
  return valid_entries(image, count, [&](Address &pc, Address &fde) {
    bool ok =
        add_signed(base, word(bytes, offset, table_width, big_endian),
                   table_width, pc) &&
        add_signed(base, word(bytes, offset + table_width, table_width,
                              big_endian),
                   table_width, fde);
    offset += 2 * table_width;
    return ok;
  });
}

static long describe(const ElfImage &image, UnwindImageView &view,
                     UnwindReadableRange *ranges) {
  __builtin_memset_inline(&view, 0, sizeof(view));
  if (!image.is_mapped())
    return EINVAL;
  view.load_bias = image.load_bias();
  view.phdr_count = image.header()->e_phnum;
  if (!image.runtime_address(image.table_address(), view.phdr_count * 56, PF_R,
                             view.phdr_address))
    return ENOEXEC;
  for (unsigned i = 0; i < view.phdr_count; ++i) {
    Elf64_Phdr p;
    if (!image.program_header(i, p))
      return ENOEXEC;
    if (p.p_type == PT_LOAD && (p.p_flags & PF_R) && p.p_memsz) {
      Address address;
      if (!image.runtime_address(p.p_vaddr, p.p_memsz, PF_R, address))
        return ENOEXEC;
      if (ranges)
        ranges[view.range_count] = {address, p.p_memsz};
      ++view.range_count;
    }
    if (p.p_type == PT_GNU_EH_FRAME) {
      if (view.eh_frame_header_size || p.p_filesz > p.p_memsz ||
          !valid_index(image, p) ||
          !image.runtime_address(p.p_vaddr, p.p_filesz, PF_R,
                                 view.eh_frame_header))
        return ENOEXEC;
      view.eh_frame_header_size = p.p_filesz;
    }
  }
  return 0;
}

long StartupUnwindImages::prepare(cpp::span<const ElfImage *const> images,
                                  Arena &arena,
                                  const StartupUnwindImages *prepared) {
  if (published || !records.empty())
    return EBUSY;
  if (images.empty() || images.size() > __SIZE_MAX__ / sizeof(Record))
    return EINVAL;
  // Build unpublished records once. The caller owns arena rollback on failure;
  // no partial set becomes visible through records or publish().
  void *storage;
  long error =
      arena.allocate(images.size() * sizeof(Record), alignof(Record), storage);
  if (error)
    return -error;
  auto *result = static_cast<Record *>(storage);
  for (size_t i = 0; i < images.size(); ++i) {
    if (!images[i] || !images[i]->header() ||
        (i == 0 ? images[i]->role() != ImageRole::MainExecutable &&
                      images[i]->role() != ImageRole::MainPIE
                : images[i]->role() != ImageRole::SharedObject &&
                      images[i]->role() != ImageRole::Interpreter))
      return EINVAL;
    auto *record = new (result + i) Record;
    record->image = images[i];
    const UnwindImageView *checked = nullptr;
    if (prepared)
      for (const auto &old : prepared->records)
        if (images[i]->same_mapped_image(*old.image)) {
          checked = &old.view;
          break;
        }
    // Each readable range comes from one program header. Allocate an upper
    // bound so describing the image need not revalidate the unwind index.
    size_t count = checked ? checked->range_count : images[i]->header()->e_phnum;
    error = arena.allocate(count * sizeof(UnwindReadableRange),
                           alignof(UnwindReadableRange), storage);
    if (error)
      return -error;
    auto *ranges = static_cast<UnwindReadableRange *>(storage);
    if (checked) {
      __builtin_memcpy_inline(&record->view, checked, sizeof(record->view));
      const auto *source = reinterpret_cast<const UnwindReadableRange *>(
          checked->readable_ranges);
      for (size_t j = 0; j < count; ++j)
        new (ranges + j) UnwindReadableRange{source[j].address, source[j].size};
    } else {
      error = describe(*images[i], record->view, ranges);
      if (error)
        return error;
    }
    record->view.readable_ranges = reinterpret_cast<Address>(storage);
    record->view.token = reinterpret_cast<Address>(record);
    // Disjoint LOAD ownership makes containing-image selection unambiguous.
    for (size_t j = 0; j < i; ++j)
      for (unsigned a = 0; a < images[i]->header()->e_phnum; ++a) {
        Elf64_Phdr p;
        images[i]->program_header(a, p);
        if (p.p_type != PT_LOAD || !p.p_memsz)
          continue;
        for (unsigned b = 0; b < images[j]->header()->e_phnum; ++b) {
          Elf64_Phdr q;
          images[j]->program_header(b, q);
          if (q.p_type != PT_LOAD || !q.p_memsz)
            continue;
          Address left = images[i]->load_bias() + p.p_vaddr;
          Address right = images[j]->load_bias() + q.p_vaddr;
          if (left < right + q.p_memsz && right < left + p.p_memsz)
            return ENOEXEC;
        }
      }
  }
  records = {result, images.size()};
  return 0;
}

bool StartupUnwindImages::publish() {
  if (published || records.empty())
    return false;
  published = true;
  return true;
}

long StartupUnwindImages::find(Address pc, UnwindImageView &out) const {
  __builtin_memset_inline(&out, 0, sizeof(out));
  if (!published)
    return EINVAL;
  for (const auto &record : records)
    if (record.image->contains_file_backed_runtime(pc, 1, PF_R | PF_X)) {
      if (!record.view.eh_frame_header_size)
        return ENOENT;
      __builtin_memcpy_inline(&out, &record.view, sizeof(out));
      return 0;
    }
  return ENOENT;
}
bool StartupUnwindImages::owns(Address token) const {
  if (published)
    for (const auto &record : records)
      if (record.view.eh_frame_header_size && token == record.view.token)
        return true;
  return false;
}
long find_unwind_image(void *context, uintptr_t pc, UnwindImageView *view) {
  if (!view)
    return EINVAL;
  __builtin_memset_inline(view, 0, sizeof(*view));
  return context ? static_cast<StartupUnwindImages *>(context)->find(pc, *view)
                 : EINVAL;
}
void release_unwind_image(void *context, uintptr_t token) {
  if (!context || !static_cast<StartupUnwindImages *>(context)->owns(token))
    fatal_startup();
  // Resident witnesses are copyable; release never drops the process pin.
}
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
