//===-- Compose the loader startup transaction
//-----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/startup.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "loader/linux/syscall.h"
#include "loader/linux/target.h"
#include "src/__support/CPP/new.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace loader {

static Bytes memory(Address address, Address size) {
  return Bytes(reinterpret_cast<const unsigned char *>(address), size);
}

// Read the path through the same fd used for identity, not a second pathname
// lookup. Discovery is optional; argv[0] is never executable provenance.
static long executable_path(OwnedFd &fd, char *path, size_t capacity) {
  char link[40] = "/proc/self/fd/";
  char digits[10];
  unsigned n = 0, value = static_cast<unsigned>(fd.get());
  do {
    digits[n++] = '0' + value % 10;
    value /= 10;
  } while (value);
  unsigned at = 14;
  while (n)
    link[at++] = digits[--n];
  link[at] = 0;
  long result;
  do {
    result = raw_syscall(SYS_readlinkat, AT_FDCWD, reinterpret_cast<long>(link),
                         reinterpret_cast<long>(path), capacity);
  } while (result == -EINTR);
  if (result < 0)
    return result;
  if (!result || static_cast<size_t>(result) >= capacity || path[0] != '/')
    return -ENOEXEC;
  constexpr char deleted[] = " (deleted)";
  if (static_cast<size_t>(result) >= sizeof(deleted) - 1) {
    bool match = true;
    for (size_t i = 0; i < sizeof(deleted) - 1; ++i)
      match &= path[result - (sizeof(deleted) - 1) + i] == deleted[i];
    if (match)
      return -ENOEXEC;
  }
  path[result] = 0;
  return result;
}

long Startup::prepare_impl(const InitialStack &input, const ElfImage &self) {
  if (!input.valid || !input.big_endian || input.value(AuxField::Secure) ||
      !is_supported_page_size(page_size) ||
      input.value(AuxField::Pagesz) != page_size)
    return -EINVAL;
  __builtin_memcpy_inline(&stack, &input, sizeof(stack));
  Address count = stack.value(AuxField::Phnum);
  if (!count || count >= PN_XNUM ||
      !is_user_range(stack.value(AuxField::Phdr), count * 56))
    return -ENOEXEC;
  Bytes phdrs = memory(stack.value(AuxField::Phdr), count * 56);
  MainLocation location;
  if (locate_main_image(stack, phdrs, location) != EntryError::None)
    return -ENOEXEC;
  Elf64_Ehdr header;
  if (!decode_elf_header(memory(location.header_runtime, 64), header) ||
      (header.e_type != ET_EXEC && header.e_type != ET_DYN))
    return -ENOEXEC;
  void *storage;
  long error = arena.allocate(count * sizeof(MappedRegion),
                              alignof(MappedRegion), storage);
  if (error)
    return error;
  auto *regions = static_cast<MappedRegion *>(storage);
  unsigned loads = 0;
  for (unsigned i = 0; i < count; ++i) {
    Elf64_Phdr p;
    if (!decode_program_header(phdrs.subspan(i * 56, 56), true, p))
      return -ENOEXEC;
    if (p.p_type != PT_LOAD || !p.p_memsz)
      continue;
    Address address;
    if (!(p.p_flags & PF_R) ||
        __builtin_add_overflow(location.bias, p.p_vaddr, &address) ||
        !is_user_range(address, p.p_memsz))
      return -ENOEXEC;
    new (regions + loads++) MappedRegion{p.p_vaddr, memory(address, p.p_memsz)};
  }
  auto role =
      header.e_type == ET_EXEC ? ImageRole::MainExecutable : ImageRole::MainPIE;
  if (ElfImage::open_mapped({regions, loads}, location.header_vaddr,
                            location.table_vaddr, role, location.bias,
                            page_size, main) != ElfError::None)
    return -ENOEXEC;
  Address self_entry;
  Address random = stack.value(AuxField::Random);
  if (!self.header() ||
      !self.runtime_address(self.header()->e_entry, 4, PF_X, self_entry) ||
      validate_entry_images(stack, location, self, main, self_entry,
                            {random, memory(random, 16)}) != EntryError::None)
    return -ENOEXEC;

  if (stack.auxv < stack.envp || (stack.auxv - stack.envp) % 8 ||
      stack.auxv - stack.envp < 8 ||
      !is_user_range(stack.envp, stack.auxv - stack.envp))
    return -ENOEXEC;
  size_t env_count = (stack.auxv - stack.envp) / 8 - 1;
  if (env_count > __SIZE_MAX__ / sizeof(Bytes))
    return -E2BIG;
  error =
      arena.allocate((env_count + 1) * sizeof(Bytes), alignof(Bytes), storage);
  if (error)
    return error;
  auto *environment = static_cast<Bytes *>(storage);
  for (size_t i = 0; i <= env_count; ++i) {
    Address pointer = 0;
    for (unsigned j = 0; j < 8; ++j)
      pointer = (pointer << 8) | memory(stack.envp + i * 8, 8)[j];
    if (i == env_count) {
      if (pointer)
        return -ENOEXEC;
      break;
    }
    // Linux limits each argument/environment string to 32 target pages.
    size_t length = 0;
    for (; length < 32 * page_size; ++length) {
      if (!is_user_range(pointer, length + 1))
        return -ENOEXEC;
      if (!memory(pointer, length + 1)[length])
        break;
    }
    if (length == 32 * page_size)
      return -E2BIG;
    new (environment + i) Bytes(memory(pointer, length));
  }

  OwnedFd fd;
  FileIdentity identity;
  bool identity_known = false;
  char path[DependencySearch::PATH_CAPACITY];
  Bytes pathname, origin;
  if (!fd.open_readonly("/proc/self/exe")) {
    identity_known = fd.identity(identity) == 0;
    long length = executable_path(fd, path, sizeof(path));
    // Metadata is optional, but acquired resources still require cleanup.
    // close consumes the descriptor even on failure; never retry it.
    error = fd.close();
    if (error)
      return error;
    if (length > 0) {
      size_t slash = length;
      while (slash && path[slash - 1] != '/')
        --slash;
      if (slash && slash != static_cast<size_t>(length)) {
        pathname = Bytes(reinterpret_cast<const unsigned char *>(path), length);
        origin = pathname.first(slash == 1 ? 1 : slash - 1);
      }
    }
  }
  SearchEnvironment search;
  error = search.parse(false, {environment, env_count}, origin);
  if (error)
    return error;
  auto result = graph.build(main, identity_known ? &identity : nullptr,
                            pathname, search);
  if (!result.error)
    result = graph.relocate();
  if (!result.error)
    result = graph.finalize_protections();
  if (result.error)
    cleanup_error = result.cleanup_error;
  if (result.error)
    return result.error;
  error = initializers.prepare(graph, arena);
  if (error)
    return error;
  if (graph.size() > __SIZE_MAX__ / sizeof(const ElfImage *))
    return -EOVERFLOW;
  void *image_storage;
  error = arena.allocate(graph.size() * sizeof(const ElfImage *),
                          alignof(const ElfImage *), image_storage);
  if (error)
    return error;
  auto **images = static_cast<const ElfImage **>(image_storage);
  size_t index = 0;
  for (auto *node = graph.scope(); node; node = node->next())
    images[index++] = node->image();
  return -unwind_images.prepare({images, index}, arena);
}

MapResult Startup::prepare(const InitialStack &input, const ElfImage &self) {
  if (started)
    return {-EBUSY, 0};
  started = true;
  long error = prepare_impl(input, self);
  if (error) {
    long release_error = reset();
    return {error, cleanup_error ? cleanup_error : release_error};
  }
  prepared = true;
  return {0, 0};
}

long Startup::run(StartupInitializers::Invoker invoke) {
  if (!prepared || running || (runtime.tp() && !runtime.is_active()))
    return -EINVAL;
  running = true;
  callbacks_started = true;
  long result = initializers.run(stack, invoke);
  running = false;
  return result;
}

long Startup::reset() {
  if (running || unwind_images.is_published())
    return -EBUSY;
  prepared = false;
  long error = runtime.reset();
  if (error)
    return error;
  error = graph.reset();
  return error ? error : arena.reset();
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
