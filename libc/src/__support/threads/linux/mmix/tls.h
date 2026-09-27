//===-- MMIX Linux checked TLS allocation -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_TLS_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_TLS_H

#include "config/linux/app.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

struct TLSHeader {
  uintptr_t dtv;
  uintptr_t thread_state;
};
static_assert(sizeof(TLSHeader) == 16 && alignof(TLSHeader) == 8);

enum class TLSError { None, InvalidImage, Overflow, MapFailed, InvalidMapping,
                      UnmapFailed };

struct TLSLayout {
  uintptr_t alignment;
  uintptr_t offset;
  uintptr_t extent;
  uintptr_t mapping_size;
};

// Shared with metadata admission so allocation cannot reject a different
// alignment-slack formula. No memory is read through image.address here.
inline TLSError tls_layout(const TLSImage &image, uintptr_t page_size,
                           TLSLayout &result) {
  constexpr uintptr_t MAX = UINTPTR_MAX >> 1;
  uintptr_t a = image.align ? image.align : 1;
  if (page_size != 8192 || (a & (a - 1)) || image.init_size > image.size ||
      (image.size && !image.address))
    return TLSError::InvalidImage;
  if (a > MAX || image.address > MAX || image.size > MAX - image.address)
    return TLSError::Overflow;
  uintptr_t r = image.address % a, h = sizeof(TLSHeader) % a;
  uintptr_t padding = r >= h ? r - h : a - (h - r);
  if (padding > MAX - sizeof(TLSHeader) ||
      image.size > MAX - sizeof(TLSHeader) - padding)
    return TLSError::Overflow;
  uintptr_t offset = sizeof(TLSHeader) + padding;
  uintptr_t extent = offset + image.size;
  uintptr_t slack = a > page_size ? a - page_size : 0;
  if (slack > MAX - extent)
    return TLSError::Overflow;
  uintptr_t size = extent + slack;
  uintptr_t rounding = (page_size - size % page_size) % page_size;
  if (rounding > MAX - size)
    return TLSError::Overflow;
  result = {a < 8 ? 8 : a, offset, extent, size + rounding};
  return TLSError::None;
}

struct TLSResult {
  TLSError error;
  long syscall_error = 0; // Positive Linux error, without touching errno.
  // A failed rollback retains ownership here, never in the success descriptor.
  TLSDescriptor retained_mapping{};
};

// The caller owns an unpublished, validated image and opaque thread state.
// Neither helper changes TP or runs TLS constructors/destructors.
TLSResult allocate_tls(const TLSImage &image, uintptr_t page_size,
                       void *thread_state, TLSDescriptor &result);
TLSResult release_tls(TLSDescriptor &mapping);

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
