//===-- Retired loader mapping and metadata release -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/runtime_load.h"
#include "loader/linux/runtime_retirement.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

long RuntimeRetirement::reclaim(ImageRegistry &registry,
                                cpp::span<RuntimeLoad *const> owners,
                                LoaderLocks::Session &session) {
  if (!session.callback_owner() || active)
    return -EBUSY;
  long error = plan.reset();
  if (!error)
    error = registry.adopt_removal(*scopes.snapshot()->registry);
  if (error)
    return error;
  // Reopen-only requests own no mappings, but still own graph/search scratch.
  // They must not wait for an image-resource entry which they will never have.
  for (auto *owner : owners) {
    if (!owner)
      continue;
    error = owner->collect_storage();
    if (error && error != -EBUSY)
      return error;
  }
  for (auto *entry = removed; entry; entry = entry->next) {
    if (entry->state == Removal::WaitingReaders ||
        entry->state == Removal::Reclaimed ||
        entry->state == Removal::Collecting)
      continue;
    if (entry->state != Removal::Ready &&
        entry->state != Removal::Quarantined &&
        entry->state != Removal::Released)
      return -EINVAL;
    if (entry->resources.empty()) {
      if (entry->images.size() > __SIZE_MAX__ / sizeof(Removal::Resource))
        return -EOVERFLOW;
      void *memory;
      error = entry->arena.allocate(entry->images.size() *
                                        sizeof(Removal::Resource),
                                    alignof(Removal::Resource), memory);
      if (error)
        return error;
      auto *resources = static_cast<Removal::Resource *>(memory);
      // Resolve every owner before the first destructive operation.
      for (size_t i = 0; i < entry->images.size(); ++i) {
        const auto *image = entry->images[i];
        if (image->startup || image->ownership.is_permanent() ||
            image->ownership.owners() ||
            image->ownership.admission() != ModuleOwnership::State::Retiring ||
            !scopes.readers_drained(image) || !queries.readers_drained(image))
          return -EBUSY;
        Address bias = image->elf.load_bias();
        RuntimeLoad *owner = nullptr;
        for (auto *load : owners)
          if (load && load->owns_mapping(bias)) {
            if (owner)
              return -EINVAL;
            owner = load;
          }
        if (!owner)
          return -ENOENT;
        new (resources + i) Removal::Resource{image, owner, bias};
      }
      entry->resources = {resources, entry->images.size()};
    }
    // A failed unmap remains a private, non-republishable quarantine. Already
    // completed mappings are skipped even if an address is reused elsewhere.
    for (auto &resource : entry->resources) {
      if (resource.image->ownership.admission() ==
          ModuleOwnership::State::Reclaimed)
        continue;
      error = resource.owner->release_mapping(resource.bias);
      if (error) {
        entry->state = Removal::Quarantined;
        entry->release_error = error;
        return error;
      }
      if (resource.image->ownership.mark_reclaimed())
        __builtin_trap();
    }
    entry->state = Removal::Released;
    entry->release_error = 0;
  }
  // Unlink callback records before releasing their shared arenas or registry
  // metadata. Completed plan references were reset before any mapping release.
  error = initializers.collect_reclaimed(session);
  if (!error)
    error = registry.collect_reclaimed();
  if (error)
    return error;
  for (auto *entry = removed; entry; entry = entry->next) {
    if (entry->state != Removal::Released &&
        entry->state != Removal::Collecting)
      continue;
    bool shared = false;
    for (auto &resource : entry->resources) {
      error = resource.owner->collect_storage();
      if (error == -EBUSY)
        shared = true; // Other images still own the same graph/search batch.
      else if (error) {
        entry->release_error = error;
        return error;
      }
    }
    if (!shared) {
      // Keep only the stable completion/error control, never stale descriptors
      // in arena memory after a partial reset. Retry the arena, not mappings.
      entry->images = {};
      entry->resources = {};
      entry->state = Removal::Collecting;
      error = entry->arena.reset();
      if (error) {
        entry->release_error = error;
        return error;
      }
      entry->state = Removal::Reclaimed;
      entry->release_error = 0;
    }
  }
  error = scopes.collect_snapshots();
  if (error && error != -EBUSY)
    return error;
  error = queries.collect_snapshots();
  return error == -EBUSY ? 0 : error;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
