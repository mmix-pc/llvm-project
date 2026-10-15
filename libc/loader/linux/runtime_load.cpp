//===-- Late dependency relocation and publication ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/runtime_load.h"
#include "hdr/errno_macros.h"
#include "loader/linux/protection.h"
#include "loader/linux/relocation.h"
#include "loader/linux/target.h"
#include "src/__support/CPP/new.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

template <class T> static long allocate(Arena &arena, size_t count, T *&out) {
  out = nullptr;
  if (!count)
    return 0;
  if (count > __SIZE_MAX__ / sizeof(T))
    return -EOVERFLOW;
  void *storage;
  long error = arena.allocate(count * sizeof(T), alignof(T), storage);
  if (!error)
    out = static_cast<T *>(storage);
  return error;
}

struct BindingContext {
  const RuntimeScopes::Snapshot &snapshot;
  size_t first;
};

static StartupRelocationError
resolve(void *opaque, size_t i, const DynamicRelocation &r, Address &out) {
  auto &context = *static_cast<BindingContext *>(opaque);
  const auto *requester = context.snapshot.registry->images[context.first + i];
  RuntimeScopes::Value value;
  const auto *provider = requester;
  Address address = requester->elf.load_bias();
  if (!is_relative_relocation(r.info) && (r.info >> 32)) {
    if (RuntimeScopes::resolve_binding(context.snapshot, requester,
                                       r.info >> 32,
                                       value) != SymbolError::None)
      return StartupRelocationError::Symbol;
    provider = value.image;
    address = value.symbol.address;
  }
  if (is_tls_relocation(r.info)) {
    if (!is_late_tls_relocation(r.info))
      return StartupRelocationError::Value;
    if ((r.info >> 32) && (!value.symbol.tls || value.symbol.weak_zero))
      return StartupRelocationError::Symbol;
    const auto &module = provider->tls;
    return tls_relocation_value(r.info, module.module_id, 0, module.memory_size,
                                (r.info >> 32) ? address : 0, r.addend, out)
               ? StartupRelocationError::None
               : StartupRelocationError::Value;
  }
  if (value.symbol.tls)
    return StartupRelocationError::Symbol;
  return runtime_relocation_value(r.info, address, r.addend, out)
             ? StartupRelocationError::None
             : StartupRelocationError::Value;
}

long RuntimeLoad::prepare_records(const ImageRegistry::Image &caller,
                                  bool global) {
  const size_t count = graph.new_images(), old_count = base->images.size();
  const RuntimeGraph::Node **nodes;
  long error = allocate(scratch, count, nodes);
  if (error)
    return error;
  size_t used = 0;
  for (auto *node = graph.scope(); node; node = node->next())
    if (!node->existing())
      new (nodes + used++) const RuntimeGraph::Node *(node);
  if (used != count)
    return -EINVAL;
  auto index = [&](const RuntimeGraph::Node *node) -> size_t {
    const auto *existing = node ? node->existing() : &caller;
    if (existing) {
      for (size_t i = 0; i < old_count; ++i)
        if (base->images[i] == existing)
          return i;
    } else {
      for (size_t i = 0; i < count; ++i)
        if (nodes[i] == node)
          return old_count + i;
    }
    return ~size_t(0);
  };
  const ElfImage **views;
  ImageRegistry::Discovery *discovery;
  SymbolObject *objects;
  cpp::span<const WritableRegion> *writes;
  error = allocate(scratch, count, views);
  if (!error)
    error = allocate(scratch, count, discovery);
  if (!error)
    error = allocate(scratch, count, objects);
  if (!error)
    error = allocate(scratch, count, writes);
  if (error)
    return error;
  for (size_t i = 0; i < count; ++i) {
    const auto &node = *nodes[i];
    const ElfImage &image = *node.image();
    if (image.revalidate_layout(page_size) != ElfError::None ||
        (node.metadata()->value(DynamicTag::Flags) & DF_STATIC_TLS))
      return -ENOEXEC;
    new (views + i) const ElfImage *(&image);
    new (objects + i) SymbolObject{&image, node.metadata()};
    size_t edges = 0;
    for (auto *edge = node.dependencies(); edge; edge = edge->next)
      ++edges;
    size_t *dependencies;
    error = allocate(scratch, edges, dependencies);
    if (error)
      return error;
    size_t j = 0;
    for (auto *edge = node.dependencies(); edge; edge = edge->next)
      new (dependencies + j++) size_t(index(edge->target));
    new (discovery + i) ImageRegistry::Discovery{true,
                                                 node.identity(),
                                                 node.path(),
                                                 index(node.load_parent()),
                                                 {dependencies, edges}};
    WritableRegion *regions;
    error = allocate(scratch, image.header()->e_phnum, regions);
    if (error)
      return error;
    size_t n = 0;
    for (unsigned ph = 0; ph < image.header()->e_phnum; ++ph) {
      Elf64_Phdr p;
      (void)image.program_header(ph, p);
      if (p.p_type != PT_LOAD || !(p.p_flags & PF_W) || !p.p_memsz)
        continue;
      Bytes bytes =
          image.mapped_memory_bytes(p.p_vaddr, p.p_memsz, PF_R | PF_W);
      if (bytes.size() != p.p_memsz)
        return -ENOEXEC;
      new (regions + n++) WritableRegion{
          p.p_vaddr, {const_cast<unsigned char *>(bytes.data()), bytes.size()}};
    }
    new (writes + i) cpp::span<const WritableRegion>(regions, n);
  }
  const auto *candidate = base;
  if (count) {
    error = registry->prepare(images, {views, count}, {discovery, count});
    if (error)
      return error;
    candidate = images.view();
  }
  const size_t root = index(graph.scope());
  if (root >= candidate->images.size())
    return -EINVAL;
  error = namespace_owner->prepare_open(*candidate, candidate->images[root],
                                        global, scopes, handle);
  if (error || !count)
    return error ? error : namespace_owner->prepare_unwind(scopes);
  BindingContext binding{*scopes.view(), old_count};
  if (relocate_objects({objects, count}, {writes, count}, resolve, &binding)
          .error != StartupRelocationError::None)
    return -ENOEXEC;
  // Template addresses now name fully relocated storage. These retained
  // templates are never used as thread instances or rewritten on reopen.
  if (!images.mark_relocated() || !images.mark_tls_ready())
    return -EINVAL;
  error = finalize_image_protections({views, count}, page_size);
  if (error)
    return error;
  return images.mark_protected() ? namespace_owner->prepare_unwind(scopes)
                                : -EINVAL;
}

MapResult RuntimeLoad::prepare(ImageRegistry &owner, RuntimeScopes &namespace_,
                               const ImageRegistry::Image &caller, Bytes name,
                               const SearchEnvironment &environment,
                               bool global, bool no_load) {
  if (active)
    return {-EBUSY, 0};
  if (!owner.snapshot() || !namespace_.snapshot() ||
      namespace_.snapshot()->registry != owner.snapshot())
    return {-EINVAL, 0};
  active = true;
  registry = &owner;
  namespace_owner = &namespace_;
  base = owner.snapshot();
  auto result = graph.build(*base, caller, name, environment, no_load);
  if (!result.error && !result.cleanup_error) {
    added = graph.new_images() != 0;
    result.error = prepare_records(caller, global);
  }
  if (result.error || result.cleanup_error) {
    long cleanup = abort();
    if (!result.cleanup_error)
      result.cleanup_error = cleanup;
  } else
    ready = true;
  return result;
}

long RuntimeLoad::commit(uint64_t &token) {
  token = 0;
  if (!ready || committed)
    return -EINVAL;
  if (registry->snapshot() != base || !namespace_owner->can_publish(scopes) ||
      (added && !registry->can_publish(images)))
    return -EAGAIN;
  if (!graph.retain())
    return -EINVAL;
  // All failure predicates were checked under the writer serializer. Neither
  // publication allocates or invokes callbacks. Only the final release-store
  // exposes the new registry, TLS identities, unwind set and lookup scopes.
  if (added)
    (void)registry->publish(images);
  (void)namespace_owner->publish(scopes);
  committed = true;
  token = handle;
  return 0;
}

long RuntimeLoad::abort() {
  if (committed)
    return -EBUSY;
  ready = false;
  long error = scopes.abort();
  if (error)
    return error;
  error = images.abort();
  if (error)
    return error;
  error = graph.reset();
  if (error)
    return error;
  error = scratch.reset();
  if (!error) {
    active = added = false;
    base = nullptr;
    registry = nullptr;
    namespace_owner = nullptr;
    handle = 0;
  }
  return error;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
