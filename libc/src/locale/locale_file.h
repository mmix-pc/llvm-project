//===-- Bounded locale file loading -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_LOCALE_FILE_H
#define LLVM_LIBC_SRC_LOCALE_LOCALE_FILE_H

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/alloc-checker.h"
#include "src/locale/locale_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// A controlled deployment mapping, not a path supplied by the application.
const char *locale_file_name(cpp::string_view name);

// Read returns ErrorOr<size_t>; a successful zero result means EOF. Check a
// trailing byte as well, so file growth cannot silently change the snapshot.
template <typename Read>
ErrorOr<LocaleDataFile *> read_locale_file(size_t size, cpp::string_view name,
                                           Read read) {
  if (size < LocaleDataView::HEADER_SIZE + LocaleDataView::DIRECTORY_SIZE ||
      size > LocaleDataView::MAX_SIZE)
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
  auto file = LocaleDataFile::copy({buffer.data, size});
  if (!file)
    return Error(file.error());
  if (cpp::string_view((*file)->view().name()) != name) {
    (*file)->release();
    return Error(EINVAL);
  }
  return file;
}

// Linux-only transport. root_fd is borrowed and must name the configured data
// root, not an application-selected directory. Neither function publishes data.
ErrorOr<LocaleDataFile *> load_locale_file_at(int root_fd,
                                              cpp::string_view name);
ErrorOr<LocaleDataFile *> load_locale_file(cpp::string_view name);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_SRC_LOCALE_LOCALE_FILE_H
