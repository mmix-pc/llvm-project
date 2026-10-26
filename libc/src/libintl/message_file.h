//===-- Bounded message object loading --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LIBINTL_MESSAGE_FILE_H
#define LLVM_LIBC_SRC_LIBINTL_MESSAGE_FILE_H

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/alloc-checker.h"
#include "src/libintl/mo_catalog.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Read returns ErrorOr<size_t>. The extra read detects growth; truncation or
// inconsistent progress is not accepted as a partially usable message object.
template <typename Read>
ErrorOr<MoCatalog *> read_message_file(size_t size, Read read) {
  if (size < MoCatalogView::HEADER_SIZE || size > MoCatalogView::MAX_SIZE)
    return Error(EINVAL);
  AllocChecker ac;
  struct Buffer {
    uint8_t *data;
    ~Buffer() { ::free(data); }
  } buffer{new (ac) uint8_t[size]};
  if (!ac)
    return Error(ENOMEM);
  size_t offset = 0;
  while (offset != size) {
    auto count = read(buffer.data + offset, size - offset);
    if (!count)
      return Error(count.error());
    if (!*count || *count > size - offset)
      return Error(EIO);
    offset += *count;
  }
  uint8_t extra;
  auto end = read(&extra, 1);
  if (!end)
    return Error(end.error());
  if (*end)
    return Error(EIO);
  return MoCatalog::copy({buffer.data, size});
}

// Linux-only transport. The path is NUL-terminated and selected by the search
// policy, not interpreted as a locale database filename. The result is owned.
ErrorOr<MoCatalog *> load_message_file(const char *path);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
