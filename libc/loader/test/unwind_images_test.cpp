//===-- Resident unwind image tests ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "elf_fixture.h"
#include "loader/linux/unwind_images.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace loader_test;
static bool fail_mapping;
namespace LIBC_NAMESPACE_DECL {
namespace loader {
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  if (fail_mapping)
    return -ENOMEM;
  long result = syscall(n, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
[[noreturn]] void fatal_startup() { _exit(127); }
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    fprintf(stderr, "unwind image check at %d\n", line);
    abort();
  }
}
struct Input {
  Fixture file;
  MappedRegion region;
  ElfImage image;
  explicit Input(ImageRole role, Address bias) : file(role) {
    file.put(56, 2, 9);
    file.segment(0, PT_PHDR, PF_R, 64, 64, 9 * 56, 9 * 56, 8);
    file.segment(8, PT_GNU_EH_FRAME, PF_R, 0xc00, 0xc00, 20, 20, 4);
    file.put(0xc00, 4, 0x011b033b);
    file.put(0xc04, 4, 0xd00 - 0xc04);
    file.put(0xc08, 4, 1);
    file.put(0xc0c, 4, 0x2000 - 0xc00);
    file.put(0xc10, 4, 0xd00 - 0xc00);
    region = {0, file.view()};
    check(ElfImage::open_mapped({&region, 1}, 0, 64, role, bias, page, image) ==
          ElfError::None);
  }
};
// Inspect real linked bytes without executing target code or consulting
// sections.
static void linked_image(const char *path) {
  FILE *input = fopen(path, "rb");
  check(input && !fseek(input, 0, SEEK_END));
  long length = ftell(input);
  check(length > 0 && !fseek(input, 0, SEEK_SET));
  std::vector<unsigned char> file(static_cast<size_t>(length));
  check(fread(file.data(), 1, file.size(), input) == file.size());
  check(!fclose(input));
  for (unsigned i = 40; i < 48; ++i)
    file[i] = 0;
  for (unsigned i = 58; i < 64; ++i)
    file[i] = 0;
  constexpr Address bias = 0x1000000;
  ElfImage image;
  check(ElfImage::open_file({file.data(), file.size()}, ImageRole::SharedObject,
                            bias, page, image) == ElfError::None);
  auto header = *image.header();
  std::vector<std::vector<unsigned char>> storage;
  storage.reserve(header.e_phnum);
  std::vector<MappedRegion> regions;
  Address pc = 0, eh = 0, header_vaddr = 0, table_vaddr = 0;
  for (unsigned i = 0; i < header.e_phnum; ++i) {
    Elf64_Phdr p;
    check(image.program_header(i, p));
    if (p.p_type == PT_GNU_EH_FRAME)
      eh = bias + p.p_vaddr;
    if (p.p_type != PT_LOAD || !p.p_memsz)
      continue;
    if (!p.p_offset) {
      header_vaddr = p.p_vaddr;
      table_vaddr = p.p_vaddr + header.e_phoff;
    }
    if ((p.p_flags & PF_X) && p.p_filesz)
      pc = bias + p.p_vaddr;
    storage.emplace_back(static_cast<size_t>(p.p_memsz), 0);
    auto &bytes = storage.back();
    for (Address j = 0; j < p.p_filesz; ++j)
      bytes[j] = file[p.p_offset + j];
    regions.push_back({p.p_vaddr, {bytes.data(), bytes.size()}});
  }
  check(pc && eh);
  check(ElfImage::open_mapped({regions.data(), regions.size()}, header_vaddr,
                              table_vaddr, ImageRole::SharedObject, bias, page,
                              image) == ElfError::None);
  Input main(ImageRole::MainPIE, 0x100000);
  const ElfImage *scope[] = {&main.image, &image};
  Arena arena(page);
  StartupUnwindImages registry;
  check(!registry.prepare(scope, arena) && registry.publish());
  UnwindImageView view;
  check(!registry.find(pc, view));
  check(view.load_bias == bias && view.eh_frame_header == eh &&
        view.phdr_address == bias + table_vaddr);
  release_unwind_image(&registry, view.token);
  puts("linked sectionless DSO unwind registry passed");
}

int main(int argc, char **argv) {
  check(argc == 1 || argc == 2);
  if (argc == 2) {
    linked_image(argv[1]);
    return 0;
  }
  static_assert(sizeof(UnwindImageView) == 64);
  static_assert(sizeof(UnwindReadableRange) == 16);
  Input main(ImageRole::MainPIE, 0x100000);
  Input dso(ImageRole::SharedObject, 0x300000);
  const ElfImage *images[] = {&main.image, &dso.image};
  Arena arena(page);
  StartupUnwindImages registry;
  UnwindImageView view{};
  check(registry.find(0x102000, view) == EINVAL);
  check(registry.prepare(images, arena) == 0);
  check(registry.prepare(images, arena) == EBUSY);
  check(registry.publish() && !registry.publish());
  for (unsigned i = 0; i < 2; ++i) {
    Address bias = i ? 0x300000 : 0x100000;
    check(find_unwind_image(&registry, bias + 0x2000, &view) == 0);
    check(view.load_bias == bias && view.phdr_address == bias + 64 &&
          view.phdr_count == 9 && view.eh_frame_header == bias + 0xc00 &&
          view.eh_frame_header_size == 20 && view.range_count == 3 &&
          registry.owns(view.token));
    auto *ranges =
        reinterpret_cast<const UnwindReadableRange *>(view.readable_ranges);
    check(ranges[0].address == bias && ranges[0].size == page);
    check(ranges[1].address == bias + 0x2000 && ranges[1].size == page);
    Address token = view.token;
    release_unwind_image(&registry, token);
    release_unwind_image(&registry,
                         token); // Copied cursor witnesses remain valid.
    check(registry.find(bias + 0x2001, view) == 0 && view.token == token);
    check(registry.find(bias + 0x2100, view) == ENOENT && !view.token);
    check(registry.find(bias + 0x4000, view) == ENOENT &&
          !view.readable_ranges);
  }
  check(!registry.owns(0) && !registry.owns(~Address(0)));
  check(find_unwind_image(nullptr, 0, &view) == EINVAL && !view.token);
  pid_t child = fork();
  check(child >= 0);
  if (!child) {
    release_unwind_image(&registry, 1);
    _exit(1);
  }
  int status;
  check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
        WEXITSTATUS(status) == 127);
  // Published records and arena deliberately remain alive until process exit.
  for (unsigned mutation = 0; mutation < 10; ++mutation) {
    Input bad(ImageRole::SharedObject, 0x300000);
    switch (mutation) {
    case 0:
      bad.file.put(0xc00, 1, 2);
      break;
    case 1:
      bad.file.put(0xc01, 1, 0);
      break;
    case 2:
      bad.file.put(0xc08, 4, 0xffffffff);
      break;
    case 3:
      bad.file.put(ph(8, 32), 8, 11);
      break;
    case 4:
      bad.file.put(0xc04, 4, 0x70000000);
      break;
    case 5:
      bad.file.put(0xc0c, 4, 0x4000 - 0xc00);
      break;
    case 6:
      bad.file.put(0xc10, 4, 0x3000 - 0xc00);
      break;
    case 7:
      bad.file.segment(7, PT_GNU_EH_FRAME, PF_R, 0xc00, 0xc00, 20, 20, 4);
      break;
    case 8:
      bad.file.put(0xc03, 1, 0xff);
      break;
    case 9:
      bad.file.put(0xc02, 1, 0xff);
      break;
    }
    const ElfImage *scope[] = {&main.image, &bad.image};
    Arena scratch(page);
    StartupUnwindImages failed;
    check(failed.prepare(scope, scratch) == ENOEXEC);
    check(!failed.publish() && failed.find(0x102000, view) == EINVAL);
    check(!scratch.reset());
  }
  Input missing(ImageRole::SharedObject, 0x500000);
  missing.file.put(ph(8), 4, PT_NULL);
  const ElfImage *scope[] = {&main.image, &missing.image};
  Arena absent_arena(page);
  StartupUnwindImages absent;
  check(!absent.prepare(scope, absent_arena) && absent.publish());
  check(absent.find(0x502000, view) == ENOENT);
  Arena failed_arena(page);
  StartupUnwindImages failed;
  fail_mapping = true;
  check(failed.prepare(images, failed_arena) == ENOMEM && !failed.publish());
  fail_mapping = false;
  check(!failed_arena.reset());
  const ElfImage *duplicate[] = {&main.image, &main.image};
  check(failed.prepare(duplicate, failed_arena) == EINVAL);
  Input overlap(ImageRole::SharedObject, 0x100000);
  const ElfImage *overlapping[] = {&main.image, &overlap.image};
  check(failed.prepare(overlapping, failed_arena) == ENOEXEC);
  // Exercise signed backward frame offsets and full-width index fields.
  Input wide(ImageRole::SharedObject, 0x700000);
  wide.file.segment(8, PT_GNU_EH_FRAME, PF_R, 0xc00, 0xc00, 36, 36, 4);
  wide.file.put(0xc00, 4, 0x011c043c);
  wide.file.put(0xc04, 8, Address(0xb00) - 0xc04);
  wide.file.put(0xc0c, 8, 1);
  wide.file.put(0xc14, 8, 0x2000 - 0xc00);
  wide.file.put(0xc1c, 8, Address(0xb00) - 0xc00);
  const ElfImage *wide_scope[] = {&main.image, &wide.image};
  check(!failed.prepare(wide_scope, failed_arena) && failed.publish());
  check(!failed.find(0x702000, view) && view.eh_frame_header_size == 36);
}
