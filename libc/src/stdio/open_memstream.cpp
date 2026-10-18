//===-- Dynamically growing memory streams --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/open_memstream.h"
#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "hdr/func/realloc.h"
#include "src/__support/File/file.h"
#include "src/__support/File/memory_file.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include "src/string/memory_utils/inline_memset.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
class DynamicMemoryFile final : public File {
  uint8_t staging[DEFAULT_BUFFER_SIZE];
  char **output;
  size_t *output_size;
  char *memory;
  size_t capacity = 1;
  size_t position = 0;
  size_t length = 0;

  static FileIOResult write(File *file, const void *data, size_t size) {
    auto *f = static_cast<DynamicMemoryFile *>(file);
    if (!size)
      return 0;
    if (size > MEMORY_FILE_MAX - f->position)
      return {0, EFBIG};
    size_t end = f->position + size;
    if (end >= f->capacity) {
      size_t capacity = f->capacity <= MEMORY_FILE_MAX / 2
                            ? f->capacity * 2
                            : MEMORY_FILE_MAX + 1;
      if (capacity < end + 1)
        capacity = end + 1;
      // Do not lose the old allocation or change the position on failure.
      char *memory = static_cast<char *>(::realloc(f->memory, capacity));
      if (!memory)
        return {0, ENOMEM};
      f->memory = memory;
      f->capacity = capacity;
    }
    if (f->position > f->length)
      inline_memset(f->memory + f->length, 0, f->position - f->length);
    inline_memcpy(f->memory + f->position, data, size);
    f->position = end;
    if (end > f->length)
      f->length = end;
    f->memory[f->length] = '\0';
    return size;
  }

  static FileIOResult read(File *, void *, size_t) { return {0, EBADF}; }

  static ErrorOr<off_t> seek(File *file, off_t offset, int whence) {
    auto *f = static_cast<DynamicMemoryFile *>(file);
    // Seeking alone does not extend the buffer. SEEK_END uses its high-water
    // length, independent of the size most recently published by fflush.
    auto result = memory_file_seek(f->position, f->length, MEMORY_FILE_MAX,
                                   offset, whence);
    if (result)
      f->position = static_cast<size_t>(result.value());
    return result;
  }

  static int flush(File *file) {
    auto *f = static_cast<DynamicMemoryFile *>(file);
    f->memory[f->length] = '\0';
    *f->output = f->memory;
    *f->output_size = f->position < f->length ? f->position : f->length;
    return 0;
  }

  static int close(File *file) {
    auto *f = static_cast<DynamicMemoryFile *>(file);
    // Closing after a failed flush must still hand the last valid allocation
    // to the caller. No shrinking realloc is needed for ownership transfer.
    flush(f);
    delete f;
    return 0;
  }

public:
  DynamicMemoryFile(char **output, size_t *size, char *memory)
      : File(write, read, seek, close, staging, sizeof(staging), _IOFBF, false,
             ModeFlags(OpenMode::WRITE), flush, /*descriptor=*/false,
             /*close_on_error=*/true),
        output(output), output_size(size), memory(memory) {
    try_set_orientation_unlocked(Orientation::BYTE);
    flush(this);
  }
};
} // namespace

LLVM_LIBC_FUNCTION(::FILE *, open_memstream, (char **buffer, size_t *size)) {
  if (!buffer || !size) {
    libc_errno = EINVAL;
    return nullptr;
  }
  char *memory = static_cast<char *>(::malloc(1));
  if (!memory) {
    libc_errno = ENOMEM;
    return nullptr;
  }
  memory[0] = '\0';
  AllocChecker ac;
  auto *file = new (ac) DynamicMemoryFile(buffer, size, memory);
  if (!ac) {
    ::free(memory);
    libc_errno = ENOMEM;
    return nullptr;
  }
  File::add_file(file);
  return reinterpret_cast<::FILE *>(file);
}
} // namespace LIBC_NAMESPACE_DECL
