//===-- MMIX Linux checked TLS allocation ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tls.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include <linux/mman.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

TLSResult release_tls(TLSDescriptor &mapping) {
  if (!mapping.size)
    return {TLSError::None};
  long ret = __llvm_libc_mmix_syscall(SYS_munmap, mapping.addr, mapping.size,
                                     0, 0, 0, 0);
  if (ret < 0)
    return {TLSError::UnmapFailed, -ret, mapping};
  mapping = {};
  return {TLSError::None};
}

TLSResult allocate_tls(const TLSImage &image, uintptr_t page_size,
                       void *thread_state, TLSDescriptor &result) {
  TLSLayout layout;
  TLSError error = tls_layout(image, page_size, layout);
  if (error != TLSError::None)
    return {error};
  long ret = __llvm_libc_mmix_syscall(
      SYS_mmap, 0, layout.mapping_size, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (static_cast<uintptr_t>(ret) >= static_cast<uintptr_t>(-4095L))
    return {TLSError::MapFailed, -ret};
  uintptr_t base = static_cast<uintptr_t>(ret);
  TLSDescriptor mapping{layout.mapping_size, base, 0};
  constexpr uintptr_t MAX = UINTPTR_MAX >> 1;
  if (!base || base % page_size || base > MAX || mapping.size > MAX - base) {
    TLSResult cleanup = release_tls(mapping);
    return cleanup.error == TLSError::None ? TLSResult{TLSError::InvalidMapping}
                                           : cleanup;
  }
  uintptr_t delta = (layout.alignment - base % layout.alignment) % layout.alignment;
  if (delta > mapping.size || layout.extent > mapping.size - delta) {
    TLSResult cleanup = release_tls(mapping);
    return cleanup.error == TLSError::None ? TLSResult{TLSError::InvalidMapping}
                                           : cleanup;
  }
  mapping.tp = base + delta;
  // Volatile byte accesses keep pre-TP initialization free of synthesized
  // memcpy/memset calls and never read the template's zero-fill tail.
  auto *dest = reinterpret_cast<volatile unsigned char *>(mapping.tp);
  for (uintptr_t i = 0; i < layout.extent; ++i)
    dest[i] = 0;
  auto *header = reinterpret_cast<TLSHeader *>(mapping.tp);
  header->thread_state = reinterpret_cast<uintptr_t>(thread_state);
  const auto *source = reinterpret_cast<const volatile unsigned char *>(image.address);
  for (uintptr_t i = 0; i < image.init_size; ++i)
    dest[layout.offset + i] = source[i];
  result = mapping;
  return {TLSError::None};
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
