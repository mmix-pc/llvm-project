//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ExceptionStorage.h"
#include <exception>
#include <new>

extern "C" long __llvm_libc_mmix_retain_exception_images(uintptr_t, uintptr_t, uint64_t*);
extern "C" long __llvm_libc_mmix_release_exception_images(uint64_t);

namespace __cxxabiv1 {
namespace mmix {
namespace {
// Keep the ordinary exception header and thrown-object alignment unchanged.
struct alignas(max_align_t) Owners {
  uint64_t metadata = 0;
  uint64_t handlers = 0;
};
void no_destructor(void*) {}

void drop(Owners* owners) {
  if (owners->handlers && __llvm_libc_mmix_release_exception_images(owners->handlers))
    std::terminate();
  if (owners->metadata && __llvm_libc_mmix_release_exception_images(owners->metadata))
    std::terminate();
  owners->handlers = owners->metadata = 0;
}
} // namespace

void* allocate_exception_storage(size_t size) {
  if (size > SIZE_MAX - sizeof(Owners))
    return nullptr;
  void* raw = __aligned_malloc_with_fallback(size + sizeof(Owners));
  if (!raw)
    return nullptr;
  return new (raw) Owners{} + 1;
}

void retain_exception_storage(
    void* storage, uintptr_t type, uintptr_t destructor, uintptr_t unexpected, uintptr_t terminate) {
  auto* owners = static_cast<Owners*>(storage) - 1;
  if (owners->metadata || owners->handlers)
    std::terminate();
  // The service retains a readable metadata image and an executable callback.
  // Trivial thrown values still need their RTTI image retained.
  if (!destructor)
    destructor = reinterpret_cast<uintptr_t>(&no_destructor);
  if (__llvm_libc_mmix_retain_exception_images(type, destructor, &owners->metadata) ||
      __llvm_libc_mmix_retain_exception_images(unexpected, terminate, &owners->handlers)) {
    drop(owners);
    std::terminate();
  }
}

void free_exception_storage(void* storage) {
  auto* owners = static_cast<Owners*>(storage) - 1;
  // Called after the final primary destructor, or after construction failed
  // before primary initialization (both tokens are then zero).
  drop(owners);
  owners->~Owners();
  __aligned_free_with_fallback(owners);
}
} // namespace mmix
} // namespace __cxxabiv1
