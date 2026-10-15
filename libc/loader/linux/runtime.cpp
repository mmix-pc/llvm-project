//===-- Startup runtime handoff -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/runtime.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/new.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {
static bool equal(Bytes bytes, const char *text) {
  size_t i = 0;
  for (; i < bytes.size(); ++i)
    if (!text[i] || bytes[i] != static_cast<unsigned char>(text[i]))
      return false;
  return text[i] == 0;
}
static Address octa(Bytes bytes, size_t offset) {
  Address value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value = (value << 8) | bytes[offset + i];
  return value;
}

// Decode only relocated bytes; execution waits for graph protection. Identity
// is its own SONAME; ordinary main-first interposition never supplies this
// hook.
static long find_bootstrap(const StartupGraph &graph, LibcBootstrap &out) {
  const StartupGraph::Node *provider = nullptr;
  bool unwind_provider = false;
  for (auto *node = graph.scope(); node; node = node->next()) {
    Bytes soname;
    const auto &metadata = *node->metadata();
    bool libc = metadata.has(DynamicTag::Soname) &&
                metadata.string(metadata.value(DynamicTag::Soname), soname) &&
                equal(soname, "libc.so.1");
    Address index;
    constexpr char name[] = "__llvm_libc_loader_bootstrap";
    bool hook = metadata.find_definition(
        {reinterpret_cast<const unsigned char *>(name), sizeof(name) - 1},
        index);
    if ((!libc && hook) || (libc && (provider || node == graph.scope())))
      return -ENOEXEC;
    if (libc)
      provider = node;
    constexpr char unwind_name[] = "__libunwind_mmix_adopt_startup_images";
    if (metadata.find_definition(
            {reinterpret_cast<const unsigned char *>(unwind_name),
             sizeof(unwind_name) - 1},
            index)) {
      Elf64_Sym symbol;
      if (unwind_provider || !metadata.has(DynamicTag::Soname) ||
          !metadata.string(metadata.value(DynamicTag::Soname), soname) ||
          !equal(soname, "libunwind.so.1") || node == graph.scope() ||
          !metadata.symbol(index, symbol) ||
          ELF64_ST_TYPE(symbol.st_info) != STT_FUNC ||
          ELF64_ST_BIND(symbol.st_info) != STB_GLOBAL ||
          ELF64_ST_VISIBILITY(symbol.st_other) != STV_PROTECTED)
        return -ENOEXEC;
      unwind_provider = true;
    }
  }
  if (!provider)
    return 0;
  if (!unwind_provider)
    return -ENOEXEC;
  const auto &image = *provider->image();
  const auto &metadata = *provider->metadata();
  Address index, runtime;
  constexpr char name[] = "__llvm_libc_loader_bootstrap";
  Elf64_Sym symbol;
  if (!metadata.find_definition(
          {reinterpret_cast<const unsigned char *>(name), sizeof(name) - 1},
          index) ||
      !metadata.symbol(index, symbol) ||
      ELF64_ST_TYPE(symbol.st_info) != STT_OBJECT ||
      ELF64_ST_BIND(symbol.st_info) != STB_GLOBAL ||
      ELF64_ST_VISIBILITY(symbol.st_other) != STV_PROTECTED ||
      symbol.st_size != sizeof(LibcBootstrap) || symbol.st_value % 8 ||
      !image.runtime_address(symbol.st_value, symbol.st_size, PF_R, runtime))
    return -ENOEXEC;
  bool relro = false;
  for (unsigned i = 0; i < image.header()->e_phnum; ++i) {
    Elf64_Phdr p;
    image.program_header(i, p);
    if (p.p_type == PT_GNU_RELRO && symbol.st_value >= p.p_vaddr &&
        symbol.st_value - p.p_vaddr <= p.p_memsz &&
        symbol.st_size <= p.p_memsz - (symbol.st_value - p.p_vaddr))
      relro = true;
  }
  auto bytes = image.mapped_memory_bytes(symbol.st_value, symbol.st_size, PF_R);
  if (!relro || bytes.size() != sizeof(LibcBootstrap) ||
      octa(bytes, 0) != sizeof(LibcBootstrap))
    return -ENOEXEC;
  Address callbacks[3];
  for (unsigned i = 0; i < 3; ++i) {
    Address pc = octa(bytes, (i + 1) * 8), checked;
    if (pc % 4 || pc < image.load_bias() ||
        !image.runtime_address(pc - image.load_bias(), 4, PF_X, checked) ||
        checked != pc)
      return -ENOEXEC;
    callbacks[i] = pc;
  }
  out = {sizeof(out),
         reinterpret_cast<decltype(out.prepare_main)>(callbacks[0]),
         reinterpret_cast<decltype(out.adopt_main)>(callbacks[1]),
         reinterpret_cast<decltype(out.abandon_main)>(callbacks[2])};
  return 0;
}

long StartupRuntime::prepare_registry(const StartupGraph &graph,
                                      const InitialStack &stack,
                                      const SearchEnvironment &search,
                                      Address bias, Arena &arena) {
  long error = find_bootstrap(graph, bootstrap);
  if (error || !bootstrap.size)
    return error;
  void *storage;
  error = arena.allocate(sizeof(RuntimeState), alignof(RuntimeState), storage);
  if (error)
    return error;
  resident = new (storage)
      RuntimeState(stack.value(AuxField::Pagesz), stack, graph.relocated_tls());
  return resident->prepare(graph, search, bias);
}

long StartupRuntime::prepare(const StartupGraph &graph,
                             const InitialStack &stack,
                             StartupUnwindImages &registry) {
  if (attempted || !graph.protections_complete())
    return -EINVAL;
  attempted = true;
  long error = find_bootstrap(graph, bootstrap);
  if (error)
    return error;
  if (!bootstrap.size) {
    // Freestanding no-TLS startup remains independent of libc. A TLS scope
    // cannot silently take that path without a runtime owner.
    for (auto *node = graph.scope(); node; node = node->next())
      if (node->image()->tls_header() || node->metadata()->requires_tls())
        return -ENOEXEC;
    return 0;
  }
  process = {stack.original_sp,
             stack.argc,
             stack.argv,
             stack.envp,
             stack.auxv,
             stack.value(AuxField::Pagesz),
             stack.value(AuxField::Random),
             stack.value(AuxField::Entry)};
  images = &registry;
  LoaderServices prepared_services{};
  prepared_services.size = sizeof(services);
  prepared_services.tls_set = graph.relocated_tls().modules();
  __builtin_memcpy_inline(&services, &prepared_services, sizeof(services));
  if (!resident)
    return -EINVAL;
  if ((error = resident->publish()))
    return error;
  resident->services(services, &registry);
  error = bootstrap.prepare_main(&process, &thread_state);
  if (error)
    return error > 0 ? -error : -EINVAL;
  if (!thread_state)
    return -EINVAL;
  error =
      services.prepare_thread_tls(services.context, thread_state, &allocation);
  return error ? -error : 0;
}

long StartupRuntime::adopt() {
  if (!allocation.tp || installed || current_thread_pointer() != allocation.tp)
    return -EINVAL;
  // No rollback may free the executing TP, even if the provider rejects it.
  installed = true;
  if (!images->publish())
    return -EINVAL;
  long error =
      bootstrap.adopt_main(&process, &services, thread_state, &allocation);
  active = error == 0;
  return error > 0 ? -error : error ? -EINVAL : 0;
}

long StartupRuntime::reset() {
  if (installed)
    return -EBUSY;
  long error = services.release_thread_tls
                   ? services.release_thread_tls(services.context, &allocation)
                   : release_thread_tls(nullptr, &allocation);
  if (error)
    return -error;
  if (thread_state) {
    bootstrap.abandon_main(thread_state);
    thread_state = nullptr;
  }
  return resident ? resident->reset() : 0;
}
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
