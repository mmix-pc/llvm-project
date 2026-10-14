//===-- Resident runtime image registry -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/image_registry.h"
#include "hdr/errno_macros.h"
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
                            cpp::span<const ElfImage *const> images) {
  using State = Transaction::State;
  if (transaction.state != State::Empty)
    return -EBUSY;
  if (images.empty())
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
    new (records + i) Image{*images[i], tls[i], !current};
    new (entries + old_count + i) const Image *(records + i);
  }
  transaction.candidate =
      new (storage) Snapshot{generation, {entries, old_count + images.size()}};
  transaction.state = State::Prepared;
  return 0;
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
