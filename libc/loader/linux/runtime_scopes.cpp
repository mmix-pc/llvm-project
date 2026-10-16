//===-- Resident runtime symbol scopes ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/runtime_scopes.h"
#include "hdr/errno_macros.h"
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

bool RuntimeScopes::Scope::contains(const Image *image) const {
  for (const Image *entry : images)
    if (entry == image)
      return true;
  return false;
}

static bool valid_name(Bytes name) {
  if (name.empty())
    return false;
  for (auto c : name)
    if (!c)
      return false;
  return true;
}

SymbolError RuntimeScopes::Scope::lookup(Bytes name, Value &out) const {
  out = {};
  if (!valid_name(name))
    return SymbolError::Index;
  SymbolValue value;
  SymbolError error = symbols.lookup(name, value);
  if (error == SymbolError::None)
    out = {value, images[value.object]};
  return error;
}

const RuntimeScopes::Context *
RuntimeScopes::Snapshot::context(const Image *image) const {
  for (const auto &entry : contexts)
    if (entry.image == image)
      return &entry;
  return nullptr;
}

long RuntimeScopes::make_scope(Arena &arena,
                               cpp::span<const Image *const> images,
                               Scope &out) {
  const Image **entries;
  SymbolObject *objects;
  DynamicInfo *metadata;
  long error = allocate(arena, images.size(), entries);
  if (!error)
    error = allocate(arena, images.size(), objects);
  if (!error)
    error = allocate(arena, images.size(), metadata);
  if (error)
    return error;
  for (size_t i = 0; i < images.size(); ++i) {
    if (!images[i] || !images[i]->discovered)
      return -EINVAL;
    for (size_t j = 0; j < i; ++j)
      if (images[j] == images[i])
        return -EINVAL;
    new (entries + i) const Image *(images[i]);
    new (metadata + i) DynamicInfo();
    if (DynamicInfo::open(images[i]->elf, metadata[i]) != DynamicError::None)
      return -ENOEXEC;
    new (objects + i) SymbolObject{&images[i]->elf, metadata + i};
  }
  out.images = {entries, images.size()};
  out.objects = {objects, images.size()};
  return StartupSymbols::open(out.objects, out.symbols, true) ==
                 SymbolError::None
             ? 0
             : -EINVAL;
}

long RuntimeScopes::closure(Arena &arena,
                            const ImageRegistry::Snapshot &registry,
                            const Image &root, Scope &out) {
  auto admitted = [&](const Image *image) {
    for (const Image *entry : registry.images)
      if (entry == image)
        return true;
    return false;
  };
  if (!admitted(&root))
    return -EINVAL;
  const Image **queue;
  long error = allocate(arena, registry.images.size(), queue);
  if (error)
    return error;
  size_t size = 1;
  new (queue) const Image *(&root);
  for (size_t i = 0; i < size; ++i) {
    if (!queue[i]->discovered)
      return -EINVAL;
    for (const Image *dependency : queue[i]->dependencies) {
      if (!admitted(dependency))
        return -EINVAL;
      size_t j = 0;
      while (j < size && queue[j] != dependency)
        ++j;
      if (j == size)
        new (queue + size++) const Image *(dependency);
    }
  }
  return make_scope(arena, {queue, size}, out);
}

long RuntimeScopes::combine(Arena &arena, const Scope &first,
                            const Scope &second, Scope &out) {
  if (first.images.size() > __SIZE_MAX__ - second.images.size())
    return -EOVERFLOW;
  const Image **entries;
  long error =
      allocate(arena, first.images.size() + second.images.size(), entries);
  if (error)
    return error;
  size_t size = 0;
  for (const Image *image : first.images)
    new (entries + size++) const Image *(image);
  for (const Image *image : second.images)
    if (!first.contains(image))
      new (entries + size++) const Image *(image);
  return make_scope(arena, {entries, size}, out);
}

long RuntimeScopes::Transaction::abort() {
  if (published)
    return -EBUSY;
  candidate = nullptr;
  long error = arena.reset();
  if (!error) {
    if (owner)
      owner->storage.cancel(batch);
    if (owner && active)
      owner->storage.release();
    batch = nullptr;
    owner = nullptr;
    base = nullptr;
    active = false;
    unwind_required = false;
    retiring = {};
  }
  return error;
}

long RuntimeScopes::begin(Transaction &transaction) {
  if (transaction.active)
    return -EBUSY;
  transaction.active = true;
  transaction.owner = this;
  storage.retain(); // A prepared writer also borrows its base generation.
  transaction.base = snapshot();
  return storage.reserve(transaction.batch);
}

long RuntimeScopes::copy_scopes(Transaction &transaction) {
  auto &next = *transaction.candidate;
  if (next.owns_registry) {
    ImageRegistry::Snapshot *registry;
    const Image **images;
    long error = allocate(transaction.arena, 1, registry);
    if (!error)
      error = allocate(transaction.arena, next.registry->images.size(), images);
    if (error)
      return error;
    for (size_t i = 0; i < next.registry->images.size(); ++i)
      new (images + i) const Image *(next.registry->images[i]);
    new (registry) ImageRegistry::Snapshot{
        next.registry->generation, {images, next.registry->images.size()}};
    next.registry = registry;
  }
  auto copy = [&](const Scope *source, const Scope *&out) -> long {
    Scope *scope;
    long error = allocate(transaction.arena, 1, scope);
    if (error)
      return error;
    new (scope) Scope{};
    error = make_scope(transaction.arena, source->images, *scope);
    out = scope;
    return error;
  };
  // No pointer to scope arrays or DynamicInfo may borrow the previous arena.
  Scope global;
  long error = make_scope(transaction.arena, next.global.images, global);
  if (error)
    return error;
  next.global = global;
  for (auto &context : next.contexts) {
    auto &entry = const_cast<Context &>(context);
    if ((error = copy(entry.local, entry.local)) ||
        (error = copy(entry.binding, entry.binding)))
      return error;
  }
  for (auto &handle : next.handles) {
    auto &entry = const_cast<Handle &>(handle);
    if ((error = copy(entry.local, entry.local)))
      return error;
  }
  transaction.unwind_required = next.unwind != nullptr;
  next.unwind = nullptr;
  return 0;
}

long RuntimeScopes::prepare_startup(const ImageRegistry::Snapshot &registry,
                                    Transaction &transaction) {
  if (snapshot() || registry.images.empty())
    return -EINVAL;
  for (const Image *image : registry.images)
    if (!image || !image->startup)
      return -EINVAL;
  long error = begin(transaction);
  if (error)
    return error;
  Snapshot *next;
  error = allocate(transaction.arena, 1, next);
  if (error)
    return error;
  new (next) Snapshot{};
  next->registry = &registry;
  error = make_scope(transaction.arena, registry.images, next->global);
  Context *contexts;
  if (!error)
    error = allocate(transaction.arena, registry.images.size(), contexts);
  if (error)
    return error;
  for (size_t i = 0; i < registry.images.size(); ++i)
    new (contexts + i)
        Context{registry.images[i], &next->global, &next->global};
  next->contexts = {contexts, registry.images.size()};
  transaction.candidate = next;
  return 0;
}

long RuntimeScopes::prepare_open(const ImageRegistry::Snapshot &registry,
                                 const Image *root, bool global,
                                 Transaction &transaction, uint64_t &token) {
  token = 0;
  const Snapshot *old = snapshot();
  if (!old || registry.images.size() < old->registry->images.size())
    return -EINVAL;
  for (const Image *image : registry.images)
    if (!image || image->ownership.admission() != ModuleOwnership::State::Live)
      return -EBUSY;
  for (size_t i = 0; i < old->registry->images.size(); ++i)
    if (old->registry->images[i] != registry.images[i])
      return -EINVAL;
  if (root) {
    bool found = false;
    for (const Image *image : registry.images)
      found |= image == root;
    if (!found)
      return -EINVAL;
  }
  // Zero and the all-ones RTLD_NEXT selector are not ordinary handle tokens.
  if (!tokens.can_reserve(2))
    return -EOVERFLOW;
  long error = begin(transaction);
  if (error)
    return error;
  Snapshot *next;
  Scope *local, *binding;
  error = allocate(transaction.arena, 1, next);
  if (!error)
    error = allocate(transaction.arena, 1, local);
  if (!error)
    error = allocate(transaction.arena, 1, binding);
  if (error)
    return error;
  new (next) Snapshot{};
  new (local) Scope{};
  new (binding) Scope{};
  next->registry = &registry;
  next->owns_registry = old->owns_registry && old->registry == &registry;
  if (root)
    error = closure(transaction.arena, registry, *root, *local);
  else
    *local = old->global;
  next->unwind = old->unwind;
  if (!error)
    error = combine(transaction.arena, old->global, *local, *binding);
  if (error)
    return error;
  next->global = global ? *binding : old->global;

  Context *contexts;
  error = allocate(transaction.arena, registry.images.size(), contexts);
  if (error)
    return error;
  for (size_t i = 0; i < registry.images.size(); ++i) {
    const Image *image = registry.images[i];
    const Context *existing = old->context(image);
    if (existing)
      new (contexts + i) Context(*existing);
    else {
      if (!local->contains(image))
        return -EINVAL;
      new (contexts + i) Context{image, local, binding};
    }
  }
  next->contexts = {contexts, registry.images.size()};
  if (old->handles.size() == __SIZE_MAX__)
    return -EOVERFLOW;
  Handle *handles;
  error = allocate(transaction.arena, old->handles.size() + 1, handles);
  if (error)
    return error;
  for (size_t i = 0; i < old->handles.size(); ++i)
    new (handles + i) Handle(old->handles[i]);
  (void)tokens.reserve(1, token);
  new (handles + old->handles.size()) Handle{token, local, !root};
  next->handles = {handles, old->handles.size() + 1};
  transaction.candidate = next;
  error = copy_scopes(transaction);
  if (error)
    transaction.candidate = nullptr;
  return error;
}

long RuntimeScopes::prepare_close(uint64_t token, Transaction &transaction) {
  const Snapshot *old = snapshot();
  if (!old || !token)
    return -EINVAL;
  size_t selected = 0;
  while (selected < old->handles.size() &&
         old->handles[selected].token != token)
    ++selected;
  if (selected == old->handles.size())
    return -EINVAL;
  long error = begin(transaction);
  if (error)
    return error;
  Snapshot *next;
  Handle *handles;
  error = allocate(transaction.arena, 1, next);
  if (!error)
    error = allocate(transaction.arena, old->handles.size() - 1, handles);
  if (error)
    return error;
  new (next) Snapshot{};
  // Bootstrap objects cannot import libc's out-of-line aggregate copy helper.
  __builtin_memcpy_inline(next, old, sizeof(Snapshot));
  size_t size = 0;
  for (size_t i = 0; i < old->handles.size(); ++i)
    if (i != selected)
      new (handles + size++) Handle(old->handles[i]);
  next->handles = {handles, size};
  Context *contexts;
  error = allocate(transaction.arena, old->contexts.size(), contexts);
  if (error)
    return error;
  for (size_t i = 0; i < old->contexts.size(); ++i)
    new (contexts + i) Context(old->contexts[i]);
  next->contexts = {contexts, old->contexts.size()};
  transaction.candidate = next;
  error = copy_scopes(transaction);
  if (error)
    transaction.candidate = nullptr;
  return error;
}

long RuntimeScopes::prepare_retire(cpp::span<const Image *const> retiring,
                                   Transaction &transaction) {
  const auto *old = snapshot();
  if (!old || retiring.empty())
    return -EINVAL;
  auto removed = [&](const Image *image) {
    for (const Image *entry : retiring)
      if (entry == image)
        return true;
    return false;
  };
  for (const auto &handle : old->handles)
    if (!handle.global_namespace)
      for (const Image *image : handle.local->images)
        if (removed(image))
          return -EBUSY;
  long error = begin(transaction);
  if (error)
    return error;
  auto &arena = transaction.arena;
  Snapshot *next;
  ImageRegistry::Snapshot *registry;
  const Image **retired;
  Context *contexts;
  Handle *handles;
  if ((error = allocate(arena, 1, next)) ||
      (error = old->registry->without(arena, retiring, registry)) ||
      (error = allocate(arena, retiring.size(), retired)) ||
      (error = allocate(arena, old->contexts.size(), contexts)) ||
      (error = allocate(arena, old->handles.size(), handles)))
    return error;
  new (next) Snapshot{};
  next->registry = registry;
  next->owns_registry = true;
  auto filter = [&](const Scope &source, Scope &out) -> long {
    const Image **images;
    long result = allocate(arena, source.images.size(), images);
    if (result)
      return result;
    size_t used = 0;
    for (const Image *image : source.images)
      if (!removed(image))
        new (images + used++) const Image *(image);
    return make_scope(arena, {images, used}, out);
  };
  if ((error = filter(old->global, next->global)))
    return error;
  size_t count = 0;
  for (const auto &context : old->contexts) {
    if (removed(context.image))
      continue;
    Scope *local, *binding;
    if ((error = allocate(arena, 1, local)) ||
        (error = allocate(arena, 1, binding)))
      return error;
    new (local) Scope{};
    new (binding) Scope{};
    if ((error = filter(*context.local, *local)) ||
        (error = filter(*context.binding, *binding)))
      return error;
    new (contexts + count++) Context{context.image, local, binding};
  }
  next->contexts = {contexts, count};
  for (size_t i = 0; i < old->handles.size(); ++i) {
    Scope *local;
    if ((error = allocate(arena, 1, local)))
      return error;
    new (local) Scope{};
    if ((error = filter(*old->handles[i].local, *local)))
      return error;
    new (handles + i)
        Handle{old->handles[i].token, local, old->handles[i].global_namespace};
  }
  next->handles = {handles, old->handles.size()};
  for (size_t i = 0; i < retiring.size(); ++i)
    new (retired + i) const Image *(retiring[i]);
  transaction.retiring = {retired, retiring.size()};
  transaction.unwind_required = old->unwind != nullptr;
  transaction.candidate = next;
  return 0;
}

bool RuntimeScopes::can_publish(const Transaction &transaction) const {
  if (transaction.owner != this || !transaction.candidate ||
      transaction.published || transaction.base != snapshot())
    return false;
  for (const Image *image : transaction.retiring)
    if (image->ownership.is_permanent() || image->ownership.owners() ||
        image->ownership.admission() != ModuleOwnership::State::Live)
      return false;
  return storage.can_publish(transaction.batch, transaction.arena) &&
         (!transaction.unwind_required || transaction.candidate->unwind) &&
         (!transaction.candidate->unwind ||
          transaction.candidate->unwind->registry ==
              transaction.candidate->registry);
}

long RuntimeScopes::publish(Transaction &transaction) {
  if (transaction.owner != this || !transaction.candidate ||
      transaction.published)
    return -EINVAL;
  if (transaction.base != snapshot())
    return -EAGAIN;
  if ((transaction.unwind_required && !transaction.candidate->unwind) ||
      (transaction.candidate->unwind &&
       transaction.candidate->unwind->registry != transaction.candidate->registry))
    return -EINVAL;
  for (const Image *image : transaction.retiring)
    if (image->ownership.is_permanent() || image->ownership.owners() ||
        image->ownership.admission() != ModuleOwnership::State::Live)
      return -EBUSY;
  if (!storage.can_publish(transaction.batch, transaction.arena))
    return -EINVAL;
  for (size_t i = 0; i < transaction.retiring.size(); ++i) {
    if (transaction.retiring[i]->ownership.retire()) {
      while (i)
        transaction.retiring[--i]->ownership.cancel_retirement();
      return -EBUSY;
    }
  }
  (void)storage.publish(transaction.batch, transaction.arena,
                        transaction.candidate);
  transaction.published = true;
  storage.release();
  return 0;
}

SymbolError RuntimeScopes::lookup(const Snapshot &snapshot, uint64_t token,
                                  Bytes name, Value &out) {
  out = {};
  for (const Handle &handle : snapshot.handles)
    if (handle.token == token)
      return (handle.global_namespace ? snapshot.global : *handle.local)
          .lookup(name, out);
  return SymbolError::Scope;
}

SymbolError RuntimeScopes::lookup_default(const Snapshot &snapshot,
                                          const Image *caller, Bytes name,
                                          Value &out) {
  out = {};
  const Context *context = snapshot.context(caller);
  if (!context)
    return SymbolError::Scope;
  SymbolError error = snapshot.global.lookup(name, out);
  return error == SymbolError::Undefined ? context->local->lookup(name, out)
                                         : error;
}

SymbolError RuntimeScopes::lookup_next(const Snapshot &snapshot,
                                       const Image *caller, Bytes name,
                                       Value &out) {
  out = {};
  const Context *context = snapshot.context(caller);
  if (!context)
    return SymbolError::Scope;
  if (!valid_name(name))
    return SymbolError::Index;
  bool after = false;
  const Scope *scopes[] = {&snapshot.global, context->local};
  for (const Scope *scope : scopes) {
    for (size_t i = 0; i < scope->images.size(); ++i) {
      const Image *image = scope->images[i];
      if (scope != &snapshot.global && snapshot.global.contains(image))
        continue;
      if (image == caller) {
        after = true;
        continue;
      }
      if (!after)
        continue;
      StartupSymbols one;
      SymbolError error =
          StartupSymbols::open({scope->objects.data() + i, 1}, one, true);
      SymbolValue value;
      if (error == SymbolError::None)
        error = one.lookup(name, value);
      if (error == SymbolError::None) {
        value.object = i;
        out = {value, image};
        return error;
      }
      if (error != SymbolError::Undefined)
        return error;
    }
  }
  return SymbolError::Undefined;
}

SymbolError RuntimeScopes::resolve_binding(const Snapshot &snapshot,
                                           const Image *requester,
                                           Address index, Value &out) {
  out = {};
  const Context *context = snapshot.context(requester);
  if (!context)
    return SymbolError::Scope;
  const Scope &scope = *context->binding;
  for (size_t i = 0; i < scope.images.size(); ++i) {
    if (scope.images[i] != requester)
      continue;
    SymbolValue value;
    SymbolError error = scope.symbols.resolve(i, index, value);
    if (error == SymbolError::None)
      out = {value, scope.images[value.object]};
    return error;
  }
  return SymbolError::Scope;
}

SymbolError RuntimeScopes::caller(const Snapshot &snapshot, Address pc,
                                  const Image *&out) {
  out = nullptr;
  if (pc & 3)
    return SymbolError::Scope;
  for (const Image *image : snapshot.registry->images) {
    for (unsigned i = 0; i < image->elf.header()->e_phnum; ++i) {
      Elf64_Phdr ph;
      image->elf.program_header(i, ph);
      Address start;
      if (ph.p_type != PT_LOAD || !(ph.p_flags & PF_X) || !ph.p_memsz ||
          !image->elf.runtime_address(ph.p_vaddr, ph.p_memsz, PF_R | PF_X,
                                      start) ||
          pc < start || pc - start >= ph.p_memsz)
        continue;
      if (out && out != image) {
        out = nullptr;
        return SymbolError::Scope;
      }
      out = image;
    }
  }
  return out ? SymbolError::None : SymbolError::Scope;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
