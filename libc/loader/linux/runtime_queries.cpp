//===-- Resident image queries --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/runtime_queries.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/new.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// A debugger breakpoint site, not an application callback.
__attribute__((noinline)) static void notify_debugger() {
  asm volatile("" ::: "memory");
}

long RuntimeQueries::abort() {
  candidate = nullptr;
  long error = pending.reset();
  if (!error) {
    snapshots.cancel(batch);
    batch = nullptr;
  }
  return error;
}

long RuntimeQueries::prepare(const ImageRegistry::Snapshot &registry) {
  if (current && current->records.size() == registry.images.size()) {
    bool same = true;
    for (size_t i = 0; i < registry.images.size(); ++i)
      same &= current->records[i].image == registry.images[i];
    if (same)
      return 0;
  }
  if (long error = abort())
    return error;
  if (long error = snapshots.reserve(batch))
    return error;
  if (registry.images.empty() ||
      registry.images.size() > __SIZE_MAX__ / sizeof(Record))
    return -EINVAL;
  void *storage;
  long error = pending.allocate(sizeof(Snapshot), alignof(Snapshot), storage);
  if (error)
    return error;
  auto *next = new (storage) Snapshot{};
  next->adds = current ? current->adds : 0;
  next->subs = current ? current->subs : 0;
  for (const auto *image : registry.images) {
    if (current && record(*current, image))
      continue;
    if (next->adds == UINT64_MAX)
      return -EOVERFLOW;
    ++next->adds;
  }
  if (current)
    for (const auto &old : current->records) {
      bool present = false;
      for (const auto *image : registry.images)
        present |= image == old.image;
      if (present)
        continue;
      if (next->subs == UINT64_MAX)
        return -EOVERFLOW;
      ++next->subs;
    }
  error = pending.allocate(sizeof(ImageRegistry::Snapshot),
                           alignof(ImageRegistry::Snapshot), storage);
  if (error)
    return error;
  auto *registry_copy = new (storage) ImageRegistry::Snapshot{};
  error = pending.allocate(registry.images.size() * sizeof(void *),
                           alignof(void *), storage);
  if (error)
    return error;
  auto **images = static_cast<const ImageRegistry::Image **>(storage);
  for (size_t i = 0; i < registry.images.size(); ++i)
    new (images + i) const ImageRegistry::Image *(registry.images[i]);
  registry_copy->generation = registry.generation;
  registry_copy->images = {images, registry.images.size()};
  next->registry = registry_copy;
  error = pending.allocate(registry.images.size() * sizeof(Record),
                           alignof(Record), storage);
  if (error)
    return error;
  auto *records = static_cast<Record *>(storage);
  next->records = {records, registry.images.size()};
  for (size_t i = 0; i < registry.images.size(); ++i) {
    const auto *image = registry.images[i];
    auto &r = *new (records + i) Record{};
    r.image = image;
    const size_t size = i ? image->path.size() : 0;
    if (size == __SIZE_MAX__)
      return -EOVERFLOW;
    error = pending.allocate(size + 1, 1, storage);
    if (error)
      return error;
    auto *name = static_cast<char *>(storage);
    for (size_t j = 0; j < size; ++j)
      name[j] = image->path[j];
    name[size] = 0;
    r.map.l_name = name;
    r.map.l_addr = image->elf.load_bias();
    r.map.l_prev = i ? &records[i - 1].map : nullptr;
    r.map.l_next =
        i + 1 < registry.images.size() ? &records[i + 1].map : nullptr;
    const auto count = image->elf.header()->e_phnum;
    error = pending.allocate(count * sizeof(Elf64_Phdr), alignof(Elf64_Phdr),
                             storage);
    if (error)
      return error;
    auto *headers = static_cast<Elf64_Phdr *>(storage);
    r.headers = headers;
    for (unsigned j = 0; j < count; ++j) {
      auto &p = *new (headers + j) Elf64_Phdr{};
      if (!image->elf.program_header(j, p))
        return -ENOEXEC;
      if (p.p_type == PT_DYNAMIC) {
        Address address;
        if (!image->elf.runtime_address(p.p_vaddr, p.p_memsz, PF_R, address))
          return -ENOEXEC;
        r.map.l_ld = reinterpret_cast<Elf64_Dyn *>(address);
      }
    }
  }
  candidate = next;
  return 0;
}

void RuntimeQueries::publish() {
  if (!candidate)
    return;
  debug.r_version = 1;
  debug.r_brk = reinterpret_cast<Address>(&notify_debugger);
  bool removing = current && candidate->subs != current->subs;
  debug.r_state =
      decltype(debug.r_state)(removing ? 2 : 1); // RT_DELETE / RT_ADD.
  notify_debugger();
  if (!snapshots.publish(batch, pending, candidate))
    fatal_startup();
  current = candidate;
  candidate = nullptr;
  batch = nullptr;
  debug.r_map = &current->records[0].map;
  debug.r_state = decltype(debug.r_state)(0); // RT_CONSISTENT.
  notify_debugger();
  // Old lists remain available to acquired readers until a writer drain.
  // Public logical close does not yet request a delete transition.
}

long RuntimeQueries::bind_debug(const ElfImage &main,
                                Address interpreter_bias) {
  if (!main.is_mapped() || (main.role() != ImageRole::MainExecutable &&
                            main.role() != ImageRole::MainPIE))
    return -EINVAL;
  DynamicInfo info;
  if (DynamicInfo::open(main, info) != DynamicError::None ||
      !info.has(DynamicTag::Debug))
    return -ENOEXEC;
  const bool big = main.header()->e_ident[EI_DATA] == ELFDATA2MSB;
  for (unsigned i = 0; i < main.header()->e_phnum; ++i) {
    Elf64_Phdr p;
    (void)main.program_header(i, p);
    if (p.p_type != PT_DYNAMIC)
      continue;
    auto bytes = main.mapped_memory_bytes(p.p_vaddr, p.p_filesz, PF_R | PF_W);
    for (size_t j = 0; j + 16 <= bytes.size(); j += 16) {
      Address tag = 0;
      for (unsigned k = 0; k < 8; ++k)
        tag |= Address(bytes[j + k]) << (8 * (big ? 7 - k : k));
      if (tag != DT_DEBUG)
        continue;
      debug.r_version = 1;
      debug.r_brk = reinterpret_cast<Address>(&notify_debugger);
      debug.r_ldbase = interpreter_bias;
      auto *destination = const_cast<unsigned char *>(bytes.data()) + j + 8;
      Address value = reinterpret_cast<Address>(&debug);
      for (unsigned k = 0; k < 8; ++k)
        destination[k] =
            static_cast<unsigned char>(value >> (8 * (big ? 7 - k : k)));
      return 0;
    }
  }
  return -ENOEXEC;
}

const RuntimeQueries::Record *
RuntimeQueries::record(const Snapshot &view,
                       const ImageRegistry::Image *image) {
  for (const auto &r : view.records)
    if (r.image == image)
      return &r;
  return nullptr;
}

long RuntimeQueries::address(const Snapshot &view, uintptr_t address,
                             Dl_info &out) {
  __builtin_memset_inline(&out, 0, sizeof(out));
  const Record *found = nullptr;
  for (const auto &r : view.records) {
    for (unsigned i = 0; i < r.image->elf.header()->e_phnum; ++i) {
      const auto &p = r.headers[i];
      const Address start = r.map.l_addr + p.p_vaddr;
      if (p.p_type == PT_LOAD && address >= start &&
          address - start < p.p_memsz) {
        if (found && found != &r)
          return EINVAL;
        found = &r;
      }
    }
  }
  if (!found)
    return ENOENT;
  out.dli_fname = found->map.l_name;
  Address base = ~Address(0);
  for (unsigned i = 0; i < found->image->elf.header()->e_phnum; ++i)
    if (found->headers[i].p_type == PT_LOAD && found->headers[i].p_memsz) {
      Address start = found->map.l_addr + found->headers[i].p_vaddr;
      if (start < base)
        base = start;
    }
  out.dli_fbase = reinterpret_cast<void *>(base);
  DynamicInfo dynamic;
  if (DynamicInfo::open(found->image->elf, dynamic) != DynamicError::None)
    return ENOEXEC;
  Address best = 0;
  bool selected = false;
  for (Address i = 1; i < dynamic.symbol_count(); ++i) {
    Elf64_Sym symbol;
    if (!dynamic.symbol(i, symbol))
      return ENOEXEC;
    unsigned type = ELF64_ST_TYPE(symbol.st_info);
    unsigned binding = ELF64_ST_BIND(symbol.st_info);
    unsigned visibility = ELF64_ST_VISIBILITY(symbol.st_other);
    if (symbol.st_shndx == SHN_UNDEF || type == STT_TLS ||
        (type != STT_NOTYPE && type != STT_OBJECT && type != STT_FUNC) ||
        (binding != STB_GLOBAL && binding != STB_WEAK) ||
        (visibility != STV_DEFAULT && visibility != STV_PROTECTED))
      continue;
    Address start = symbol.st_value;
    if (symbol.st_shndx != SHN_ABS) {
      if (start > ~Address(0) - found->map.l_addr)
        continue;
      start += found->map.l_addr;
    }
    if (start > address || (selected && start <= best))
      continue;
    Bytes name;
    if (!dynamic.string(symbol.st_name, name) || name.empty())
      continue;
    selected = true;
    best = start;
    out.dli_sname = reinterpret_cast<const char *>(name.data());
    out.dli_saddr = reinterpret_cast<void *>(start);
  }
  return 0;
}

long RuntimeQueries::phdr_info(const Snapshot &view, const Record &record,
                               void *context, PeekTLS peek, dl_phdr_info &out) {
  __builtin_memset_inline(&out, 0, sizeof(out));
  out.dlpi_addr = record.map.l_addr;
  out.dlpi_name = record.map.l_name;
  out.dlpi_phdr = record.headers;
  out.dlpi_phnum = record.image->elf.header()->e_phnum;
  out.dlpi_adds = view.adds;
  out.dlpi_subs = view.subs;
  out.dlpi_tls_modid =
      record.image->elf.tls_header() ? record.image->tls.module_id : 0;
  if (out.dlpi_tls_modid) {
    if (!peek)
      return ENOSYS;
    out.dlpi_tls_data = peek(context, &record.image->tls);
  }
  return 0;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
