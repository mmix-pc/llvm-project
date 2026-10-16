//===-- Resident runtime image registry -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/image_registry.h"
#include "hdr/errno_macros.h"
#include "loader/linux/dynamic.h"
#include "src/__support/CPP/new.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

static bool same_image(const ElfImage &a, const ElfImage &b) {
  return a.load_bias() == b.load_bias() &&
         a.file_backed_bytes(a.table_address(), 1).data() ==
             b.file_backed_bytes(b.table_address(), 1).data();
}

bool RegistrySequence::reserve(uint64_t count, uint64_t &first) {
  if (!can_reserve(count))
    return false;
  first = next;
  uint64_t last = next + count - 1;
  next = last == ~uint64_t(0) ? 0 : last + 1;
  return true;
}

const ImageRegistry::Image *
ImageRegistry::Snapshot::find(uint64_t id, uint64_t module_generation) const {
  for (const Image *image : images)
    if (image->tls.module_id == id &&
        image->tls.generation == module_generation)
      return image;
  return nullptr;
}

long ImageRegistry::Snapshot::reclaimable(
    Arena &scratch, cpp::span<const Image *const> roots,
    cpp::span<const Image *const> &out, bool for_finalization) const {
  out = {};
  const size_t count = images.size();
  if (count > __SIZE_MAX__ / sizeof(Image *))
    return -EOVERFLOW;
  auto index = [&](const Image *image) {
    for (size_t i = 0; i < count; ++i)
      if (images[i] == image)
        return i;
    return count;
  };
  for (size_t i = 0; i < count; ++i) {
    const Image *image = images[i];
    if (!image || index(image) != i)
      return -EINVAL;
    for (const Image *dependency : image->dependencies)
      if (index(dependency) == count)
        return -EINVAL;
    for (const Binding *edge = image->bindings; edge; edge = edge->next)
      if (index(edge->provider) == count)
        return -EINVAL;
  }
  for (const Image *root : roots)
    if (index(root) == count)
      return -EINVAL;
  if (!count)
    return 0;
  void *storage;
  long error = scratch.allocate(count, 1, storage);
  if (error)
    return error;
  auto *live = static_cast<unsigned char *>(storage);
  for (size_t i = 0; i < count; ++i)
    live[i] = 0;
  error = scratch.allocate(count * sizeof(Image *), alignof(Image *), storage);
  if (error)
    return error;
  auto **queue = static_cast<const Image **>(storage);
  size_t used = 0;
  auto mark = [&](const Image *image) {
    size_t i = index(image);
    if (!live[i]) {
      live[i] = 1;
      new (queue + used++) const Image *(image);
    }
  };
  for (const Image *image : images)
    if (image->ownership.is_permanent() ||
        image->ownership.owners() >
            (for_finalization ? image->ownership.owners(
                                    ModuleOwnership::Kind::ProcessCallback)
                              : 0))
      mark(image);
  for (const Image *root : roots)
    mark(root);
  // Each image enters the queue once, regardless of cycles or duplicate edges.
  for (size_t i = 0; i < used; ++i) {
    for (const Image *dependency : queue[i]->dependencies)
      mark(dependency);
    for (const Binding *edge = queue[i]->bindings; edge; edge = edge->next)
      mark(edge->provider);
  }
  used = 0;
  for (size_t i = 0; i < count; ++i)
    if (!live[i] && images[i]->ownership.admission() !=
                        ModuleOwnership::State::Reclaimed)
      new (queue + used++) const Image *(images[i]);
  out = {queue, used};
  return 0;
}

long ImageRegistry::Snapshot::without(Arena &arena,
                                      cpp::span<const Image *const> removed,
                                      Snapshot *&out) const {
  out = nullptr;
  if (removed.empty() || images.size() > __SIZE_MAX__ / sizeof(Image *))
    return -EINVAL;
  auto excluded = [&](const Image *image) {
    for (const Image *entry : removed)
      if (entry == image)
        return true;
    return false;
  };
  for (size_t i = 0; i < removed.size(); ++i) {
    const Image *image = removed[i];
    bool found = false;
    for (const Image *entry : images)
      found |= entry == image;
    if (!image || !found)
      return -EINVAL;
    for (size_t j = 0; j < i; ++j)
      if (removed[j] == image)
        return -EINVAL;
    if (image->startup || image->ownership.is_permanent() ||
        image->ownership.owners() ||
        image->ownership.admission() != ModuleOwnership::State::Live)
      return -EBUSY;
  }
  for (const Image *image : images) {
    if (excluded(image))
      continue;
    for (const Image *dependency : image->dependencies)
      if (excluded(dependency))
        return -EBUSY;
    for (const Binding *binding = image->bindings; binding;
         binding = binding->next)
      if (excluded(binding->provider))
        return -EBUSY;
  }
  void *memory;
  long error = arena.allocate(sizeof(Snapshot), alignof(Snapshot), memory);
  if (error)
    return error;
  auto *result = new (memory) Snapshot{generation, {}};
  if (!images.empty()) {
    error = arena.allocate(images.size() * sizeof(Image *), alignof(Image *),
                           memory);
    if (error)
      return error;
    auto **entries = static_cast<const Image **>(memory);
    size_t size = 0;
    for (const Image *image : images)
      if (!excluded(image))
        new (entries + size++) const Image *(image);
    result->images = {entries, size};
  }
  out = result;
  return 0;
}

long ImageRegistry::record_binding(Transaction &transaction,
                                   const Image *requester,
                                   const Image *provider) {
  if (transaction.owner != this ||
      transaction.state != Transaction::State::Prepared || !requester ||
      !provider)
    return -EINVAL;
  bool new_requester = false, known_provider = false;
  const size_t first = transaction.base ? transaction.base->images.size() : 0;
  const auto &images = transaction.candidate->images;
  for (size_t i = 0; i < images.size(); ++i) {
    new_requester |= i >= first && images[i] == requester;
    known_provider |= images[i] == provider;
  }
  if (!new_requester || !known_provider)
    return -EINVAL;
  if (!provider->ownership.available_for_load())
    return -EBUSY;
  if (requester == provider)
    return 0;
  for (const Binding *edge = requester->bindings; edge; edge = edge->next)
    if (edge->provider == provider)
      return 0;
  void *storage;
  long error =
      transaction.arena.allocate(sizeof(Binding), alignof(Binding), storage);
  if (error)
    return error;
  requester->bindings = new (storage) Binding{provider, requester->bindings};
  return 0;
}

const ImageRegistry::Snapshot *ImageRegistry::Transaction::view() const {
  return state == State::Empty || state == State::Aborting ? nullptr
                                                           : candidate;
}

bool ImageRegistry::Transaction::mark_relocated() {
  if (state != State::Prepared)
    return false;
  state = State::Relocated;
  return true;
}
bool ImageRegistry::Transaction::mark_tls_ready() {
  if (state != State::Relocated)
    return false;
  state = State::TLSReady;
  return true;
}
bool ImageRegistry::Transaction::mark_protected() {
  if (state != State::TLSReady)
    return false;
  state = State::Protected;
  return true;
}

long ImageRegistry::Transaction::abort() {
  if (state == State::Published)
    return -EBUSY;
  state = State::Aborting;
  candidate = nullptr;
  long error = arena.reset();
  if (!error) {
    state = State::Empty;
    owner = nullptr;
    base = nullptr;
  }
  return error;
}

long ImageRegistry::prepare(Transaction &transaction,
                            cpp::span<const ElfImage *const> images,
                            cpp::span<const Discovery> discovery) {
  using State = Transaction::State;
  if (transaction.state != State::Empty)
    return -EBUSY;
  if (images.empty())
    return -EINVAL;
  if (!discovery.empty() && discovery.size() != images.size())
    return -EINVAL;
  const size_t old_count = current ? current->images.size() : 0;
  const size_t max = ~size_t(0);
  if (images.size() > max - old_count || images.size() > max / sizeof(Image) ||
      images.size() > max / sizeof(StartupTLSModule) ||
      images.size() + old_count > max / sizeof(Image *) ||
      !ids.can_reserve(images.size()) || !generations.can_reserve(1))
    return -EOVERFLOW;
  for (size_t i = 0; i < images.size(); ++i) {
    const ElfImage *image = images[i];
    if (!image || !image->header() ||
        (!current && i == 0 ? image->role() != ImageRole::MainExecutable &&
                                  image->role() != ImageRole::MainPIE
                            : image->role() != ImageRole::SharedObject))
      return -EINVAL;
    // An already admitted view must not acquire another identity. Discovery
    // also checks fd-derived file identities before reaching this component.
    for (size_t j = 0; j < i; ++j)
      if (same_image(*images[j], *image))
        return -EEXIST;
    if (current)
      for (const Image *old : current->images)
        if (same_image(old->elf, *image))
          return -EEXIST;
    if (!discovery.empty()) {
      const auto &info = discovery[i];
      if ((!info.path.empty() && info.path[0] != '/') ||
          (info.parent != ~size_t(0) && info.parent >= old_count + i))
        return -EINVAL;
      for (auto c : info.path)
        if (!c)
          return -EINVAL;
      for (size_t dependency : info.dependencies)
        if (dependency >= old_count + images.size())
          return -EINVAL;
      if (info.identity_known) {
        for (size_t j = 0; j < i; ++j)
          if (discovery[j].identity_known &&
              discovery[j].identity == info.identity)
            return -EEXIST;
        if (current)
          for (const Image *old : current->images)
            if (old->identity_known && old->identity == info.identity)
              return -EEXIST;
      }
    }
  }

  // Failed preparation remains explicitly abortable, including failed unmaps.
  transaction.owner = this;
  transaction.base = current;
  transaction.state = State::Aborting;
  void *storage;
  long error =
      transaction.arena.allocate(images.size() * sizeof(StartupTLSModule),
                                 alignof(StartupTLSModule), storage);
  if (error)
    return error;
  auto *tls = static_cast<StartupTLSModule *>(storage);
  for (size_t i = 0; i < images.size(); ++i)
    new (tls + i) StartupTLSModule{};
  if (!current) {
    StartupTLSSet set;
    auto result = plan_startup_tls(images, {tls, images.size()}, set);
    if (result != TLSLayoutError::None)
      return result == TLSLayoutError::Overflow ? -EOVERFLOW : -EINVAL;
  } else {
    for (size_t i = 0; i < images.size(); ++i) {
      Address unused_cursor = 0;
      if (!place_tls_module(*images[i], unused_cursor, tls[i]))
        return -EOVERFLOW;
      tls[i].tp_offset = 0;
    }
  }
  error = transaction.arena.allocate(images.size() * sizeof(Image),
                                     alignof(Image), storage);
  if (error)
    return error;
  auto *records = static_cast<Image *>(storage);
  error = transaction.arena.allocate(
      (old_count + images.size()) * sizeof(Image *), alignof(Image *), storage);
  if (error)
    return error;
  auto **entries = static_cast<const Image **>(storage);
  error =
      transaction.arena.allocate(sizeof(Snapshot), alignof(Snapshot), storage);
  if (error)
    return error;

  uint64_t first = 1, generation = 1;
  // Both counters were preflighted; caller serialization excludes interference.
  // Startup identities are fixed and are reserved only at its first commit.
  if (current) {
    (void)ids.reserve(images.size(), first);
    (void)generations.reserve(1, generation);
  }
  for (size_t i = 0; i < old_count; ++i)
    new (entries + i) const Image *(current->images[i]);
  for (size_t i = 0; i < images.size(); ++i) {
    tls[i].module_id = first + i;
    tls[i].generation = generation;
    DynamicInfo dynamic;
    bool nodelete = DynamicInfo::open(*images[i], dynamic) == DynamicError::None &&
                    (dynamic.value(DynamicTag::Flags1) & DF_1_NODELETE);
    new (records + i) Image{*images[i], tls[i], !current,
                           {tls[i].module_id, tls[i].generation,
                            !current || nodelete, ~uint64_t(0), events}};
    new (entries + old_count + i) const Image *(records + i);
  }
  transaction.candidate =
      new (storage) Snapshot{generation, {entries, old_count + images.size()}};
  for (size_t i = 0; i < discovery.size(); ++i) {
    auto &record = records[i];
    const auto &info = discovery[i];
    record.identity_known = info.identity_known;
    record.identity = info.identity;
    record.parent = info.parent == ~size_t(0) ? nullptr : entries[info.parent];
    if (!info.path.empty()) {
      error = transaction.arena.allocate(info.path.size(), 1, storage);
      if (error)
        return error;
      auto *path = static_cast<unsigned char *>(storage);
      for (size_t j = 0; j < info.path.size(); ++j)
        path[j] = info.path[j];
      record.path = {path, info.path.size()};
    }
    if (!info.dependencies.empty()) {
      if (info.dependencies.size() > max / sizeof(Image *))
        return -EOVERFLOW;
      error =
          transaction.arena.allocate(info.dependencies.size() * sizeof(Image *),
                                     alignof(Image *), storage);
      if (error)
        return error;
      auto **edges = static_cast<const Image **>(storage);
      size_t used = 0;
      for (size_t dependency : info.dependencies) {
        const Image *target = entries[dependency];
        size_t j = 0;
        while (j < used && edges[j] != target)
          ++j;
        if (j == used)
          new (edges + used++) const Image *(target);
      }
      record.dependencies = {edges, used};
    }
    record.discovered = true;
  }
  transaction.state = State::Prepared;
  return 0;
}

bool ImageRegistry::can_publish(const Transaction &transaction) const {
  return transaction.owner == this &&
         transaction.state == Transaction::State::Protected &&
         transaction.base == current && resident.can_adopt(transaction.arena);
}

long ImageRegistry::publish(Transaction &transaction) {
  if (transaction.owner != this ||
      transaction.state != Transaction::State::Protected)
    return -EINVAL;
  if (transaction.base != current)
    return -EAGAIN;
  if (!resident.adopt(transaction.arena))
    return -EINVAL;
  if (!current) {
    uint64_t unused;
    (void)ids.reserve(transaction.candidate->images.size(), unused);
    (void)generations.reserve(1, unused);
  }
  current = transaction.candidate;
  transaction.state = Transaction::State::Published;
  return 0;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
