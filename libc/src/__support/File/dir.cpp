//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implementation of the platform independent Dir class.
///
//===----------------------------------------------------------------------===//

#include "src/__support/File/dir.h"

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/CPP/mutex.h" // lock_guard
#include "src/__support/CPP/new.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {

ErrorOr<Dir *> Dir::fdopen(int fd) {
  auto position = platform_seekdir(fd, 0, SEEK_CUR);
  if (!position)
    return Error(position.error());
  LIBC_NAMESPACE::AllocChecker ac;
  Dir *dir = new (ac) Dir(fd);
  if (!ac)
    return LIBC_NAMESPACE::Error(ENOMEM);
  dir->position = position.value();
  return dir;
}

ErrorOr<Dir *> Dir::open(const char *path) {
  auto fd = platform_opendir(path);
  if (!fd)
    return LIBC_NAMESPACE::Error(fd.error());

  auto result = Dir::fdopen(fd.value());
  if (!result)
    platform_closedir(fd.value());
  return result;
}

ErrorOr<struct dirent *> Dir::read() {
  cpp::lock_guard lock(mutex);
  return read_unlocked();
}

ErrorOr<struct dirent *> Dir::read_unlocked() {
  if (readptr >= fillsize) {
    auto readsize = platform_fetch_dirents(fd, buffer);
    if (!readsize)
      return LIBC_NAMESPACE::Error(readsize.error());
    if (readsize.value() > BUFSIZE)
      return Error(EIO);
    fillsize = readsize.value();
    readptr = 0;
  }
  if (fillsize == 0)
    return nullptr;

  cpp::span<uint8_t> buf_span(buffer, BUFSIZE);

  constexpr size_t NAME_OFFSET = offsetof(struct dirent, d_name);
  if (fillsize - readptr < NAME_OFFSET + 1)
    return Error(EIO);

  struct dirent *d =
      reinterpret_cast<struct dirent *>(buf_span.subspan(readptr).data());

  size_t reclen = platform_dir_reclen(d);

  if (reclen < NAME_OFFSET + 1 || reclen > fillsize - readptr ||
      reclen % alignof(struct dirent) != 0)
    return Error(EIO);

  size_t name_length = 0;
  while (name_length < reclen - NAME_OFFSET && d->d_name[name_length])
    ++name_length;
  if (name_length == reclen - NAME_OFFSET)
    return Error(EIO);

  position = platform_dir_offset(d);
  readptr += reclen;
  return d;
}

ErrorOr<struct dirent *> Dir::read_copy(struct dirent *entry) {
  cpp::lock_guard lock(mutex);
  auto result = read_unlocked();
  if (!result || !result.value())
    return result;
  const auto *source = result.value();
  size_t length = 0;
  while (source->d_name[length])
    ++length;
  // The caller owns name-sized storage, not necessarily record-padding space.
  inline_memcpy(entry, source, offsetof(struct dirent, d_name) + length + 1);
  return entry;
}

ErrorOr<long> Dir::tell() {
  cpp::lock_guard lock(mutex);
  if (position > cpp::numeric_limits<long>::max() ||
      position < cpp::numeric_limits<long>::min())
    return Error(EOVERFLOW);
  return static_cast<long>(position);
}

int Dir::seek(long offset) {
  cpp::lock_guard lock(mutex);
  auto result = platform_seekdir(fd, offset, SEEK_SET);
  if (!result)
    return result.error();
  position = result.value();
  readptr = fillsize = 0;
  return 0;
}

int Dir::close(bool consume_on_error) {
  int retval;
  {
    cpp::lock_guard lock(mutex);
    retval = platform_closedir(fd);
    if (retval && !consume_on_error)
      return retval;
  }
  delete this;
  return retval;
}

} // namespace LIBC_NAMESPACE_DECL
