//===-- Resident runtime unwind lookup ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/runtime_unwind.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/new.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

long RuntimeScopes::prepare_unwind(Transaction &transaction) {
  if (transaction.owner != this || !transaction.candidate ||
      transaction.published || transaction.base != snapshot())
    return -EINVAL;
  auto &next = *transaction.candidate;
  transaction.unwind_required = true;
  if (next.unwind && next.unwind->registry == next.registry)
    return 0; // Reopen, promotion and logical close retain the same witnesses.
  const auto &images = next.registry->images;
  if (images.empty() || images.size() > __SIZE_MAX__ / sizeof(const ElfImage *))
    return -EINVAL;
  void *memory;
  long error =
      transaction.arena.allocate(images.size() * sizeof(const ElfImage *),
                                 alignof(const ElfImage *), memory);
  if (error)
    return error;
  auto **views = static_cast<const ElfImage **>(memory);
  for (size_t i = 0; i < images.size(); ++i) {
    if (!images[i])
      return -EINVAL;
    new (views + i) const ElfImage *(&images[i]->elf);
  }
  error = transaction.arena.allocate(sizeof(UnwindState), alignof(UnwindState),
                                     memory);
  if (error)
    return error;
  auto *state = new (memory) UnwindState;
  state->registry = next.registry;
  error = state->images.prepare({views, images.size()}, transaction.arena);
  if (error)
    return -error;
  if (!state->images.publish())
    return -EINVAL;
  // This marks only the private immutable set ready. The scope release-store
  // publishes image, TLS, symbol and unwind metadata together after protection.
  next.unwind = state;
  return 0;
}

long find_runtime_unwind_image(void *context, uintptr_t pc,
                               UnwindImageView *out) {
  if (!out)
    return EINVAL;
  __builtin_memset_inline(out, 0, sizeof(*out));
  if (!context)
    return EINVAL;
  auto lease = static_cast<RuntimeScopes *>(context)->acquire();
  const auto *snapshot = lease.get();
  long error = snapshot && snapshot->unwind
                   ? snapshot->unwind->images.find(pc, *out)
                   : EINVAL;
  if (!error)
    lease.pin(); // FIXME: Copied public cursors still lack release tracking.
  return error;
}

bool RuntimeScopes::owns_unwind_token(uintptr_t token) const {
  return storage.pinned_contains([&](const Snapshot &view) {
    return view.unwind && view.unwind->images.owns(token);
  });
}

void release_runtime_unwind_image(void *context, uintptr_t token) {
  if (context && token) {
    auto &scopes = *static_cast<RuntimeScopes *>(context);
    auto lease = scopes.acquire();
    if (scopes.owns_unwind_token(token))
      return;
  }
  fatal_startup();
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
