//===-- Fixed-size memory streams -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/fmemopen.h"
#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "src/__support/File/file.h"
#include "src/__support/File/memory_file.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include "src/string/memory_utils/inline_memset.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
class FixedMemoryFile final : public File {
  uint8_t staging[DEFAULT_BUFFER_SIZE];
  char *memory;
  size_t capacity;
  size_t position = 0;
  size_t length = 0;
  bool owns_memory;
  bool append;
  bool needs_termination = false;

  static FileIOResult read(File *file, void *data, size_t size) {
    auto *f = static_cast<FixedMemoryFile *>(file);
    size_t remaining = f->position < f->length ? f->length - f->position : 0;
    size_t count = size < remaining ? size : remaining;
    if (count)
      inline_memcpy(data, f->memory + f->position, count);
    f->position += count;
    return count;
  }

  static FileIOResult write(File *file, const void *data, size_t size) {
    auto *f = static_cast<FixedMemoryFile *>(file);
    if (!size)
      return 0;
    size_t start = f->append ? f->length : f->position;
    size_t available = f->capacity - start;
    size_t count = size < available ? size : available;
    if (!count)
      return {0, ENOSPC};
    if (start > f->length)
      inline_memset(f->memory + f->length, 0, start - f->length);
    inline_memcpy(f->memory + start, data, count);
    f->position = start + count;
    if (f->position > f->length) {
      f->length = f->position;
      f->needs_termination = true;
    }
    // Synchronize here too: a buffer flush preceding a seek must terminate
    // the newly extended data, not the position selected by the seek.
    flush(f);
    return {count, count == size ? 0 : ENOSPC};
  }

  static ErrorOr<off_t> seek(File *file, off_t offset, int whence) {
    auto *f = static_cast<FixedMemoryFile *>(file);
    auto result =
        memory_file_seek(f->position, f->length, f->capacity, offset, whence);
    if (result)
      f->position = static_cast<size_t>(result.value());
    return result;
  }

  static int flush(File *file) {
    auto *f = static_cast<FixedMemoryFile *>(file);
    if (f->needs_termination && f->capacity) {
      // A terminator must not replace a byte already accepted by the write.
      if (f->length < f->capacity)
        f->memory[f->length] = '\0';
      f->needs_termination = false;
    }
    return 0;
  }

  static int close(File *file) {
    auto *f = static_cast<FixedMemoryFile *>(file);
    if (f->owns_memory)
      ::free(f->memory);
    delete f;
    return 0;
  }

public:
  FixedMemoryFile(char *memory, size_t size, ModeFlags mode, bool owned)
      : File(write, read, seek, close, staging, sizeof(staging), _IOFBF, false,
             mode, flush, /*descriptor=*/false, /*close_on_error=*/true),
        memory(memory), capacity(size), owns_memory(owned),
        append(mode & ModeFlags(OpenMode::APPEND)) {
    if (mode & ModeFlags(OpenMode::READ))
      length = size;
    else if (append) {
      while (length < size && memory[length])
        ++length;
      position = length;
    } else if (size) {
      memory[0] = '\0';
    }
    try_set_orientation_unlocked(Orientation::BYTE);
  }
};
} // namespace

LLVM_LIBC_FUNCTION(::FILE *, fmemopen,
                   (void *__restrict buffer, size_t size,
                    const char *__restrict mode)) {
  auto flags = mode ? File::mode_flags(mode) : 0;
  if (!flags || size > MEMORY_FILE_MAX) {
    libc_errno = EINVAL;
    return nullptr;
  }
  char *memory = static_cast<char *>(buffer);
  if (!buffer) {
    memory = static_cast<char *>(::malloc(size ? size : 1));
    if (!memory) {
      libc_errno = ENOMEM;
      return nullptr;
    }
    inline_memset(memory, 0, size ? size : 1);
  }
  AllocChecker ac;
  auto *file = new (ac) FixedMemoryFile(memory, size, flags, !buffer);
  if (!ac) {
    if (!buffer)
      ::free(memory);
    libc_errno = ENOMEM;
    return nullptr;
  }
  File::add_file(file);
  return reinterpret_cast<::FILE *>(file);
}
} // namespace LIBC_NAMESPACE_DECL
