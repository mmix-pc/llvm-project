//===-- Dynamically growing wide memory streams ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/open_wmemstream.h"
#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "hdr/func/realloc.h"
#include "src/__support/File/file.h"
#include "src/__support/File/memory_file.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
namespace {

class WideMemoryFile final : public File {
  // Reserve a terminator and keep allocation and pointer differences bounded.
  static constexpr size_t MAX_LENGTH = MEMORY_FILE_MAX / sizeof(wchar_t) - 1;
  wchar_t **output;
  size_t *output_size;
  wchar_t *memory;
  size_t capacity = 1;
  size_t position = 0;
  size_t length = 0;

  static FileIOResult write_wide(File *file, const wchar_t *data, size_t size) {
    auto *f = static_cast<WideMemoryFile *>(file);
    if (size > MAX_LENGTH - f->position)
      return {0, EFBIG};
    size_t end = f->position + size;
    if (end >= f->capacity) {
      size_t capacity =
          f->capacity <= MAX_LENGTH / 2 ? f->capacity * 2 : MAX_LENGTH + 1;
      if (capacity <= end)
        capacity = end + 1;
      auto *memory = static_cast<wchar_t *>(
          ::realloc(f->memory, capacity * sizeof(wchar_t)));
      if (!memory)
        return {0, ENOMEM};
      f->memory = memory;
      f->capacity = capacity;
    }
    for (size_t i = f->length; i < f->position; ++i)
      f->memory[i] = L'\0';
    for (size_t i = 0; i < size; ++i)
      f->memory[f->position + i] = data[i];
    f->position = end;
    if (end > f->length)
      f->length = end;
    f->memory[f->length] = L'\0';
    return size;
  }

  static FileIOResult write_bytes(File *, const void *, size_t) {
    return {0, EBADF};
  }
  static FileIOResult read_bytes(File *, void *, size_t) { return {0, EBADF}; }

  static ErrorOr<off_t> seek(File *file, off_t offset, int whence) {
    auto *f = static_cast<WideMemoryFile *>(file);
    auto result =
        memory_file_seek(f->position, f->length, MAX_LENGTH, offset, whence);
    if (result)
      f->position = static_cast<size_t>(result.value());
    return result;
  }

  static int flush(File *file) {
    auto *f = static_cast<WideMemoryFile *>(file);
    f->memory[f->length] = L'\0';
    *f->output = f->memory;
    *f->output_size = f->position < f->length ? f->position : f->length;
    return 0;
  }

  static int close(File *file) {
    auto *f = static_cast<WideMemoryFile *>(file);
    flush(f);
    return 0;
  }

  static void destroy(File *file) {
    delete static_cast<WideMemoryFile *>(file);
  }

public:
  WideMemoryFile(wchar_t **output, size_t *size, wchar_t *memory)
      : File(write_bytes, read_bytes, seek, close, nullptr, 0, _IONBF, false,
             ModeFlags(OpenMode::WRITE), flush, /*descriptor=*/false,
             write_wide, destroy),
        output(output), output_size(size), memory(memory) {
    try_set_orientation_unlocked(Orientation::WIDE);
    flush(this);
  }
};
} // namespace

LLVM_LIBC_FUNCTION(::FILE *, open_wmemstream,
                   (wchar_t * *buffer, size_t *size)) {
  if (!buffer || !size) {
    libc_errno = EINVAL;
    return nullptr;
  }
  auto *memory = static_cast<wchar_t *>(::malloc(sizeof(wchar_t)));
  if (!memory) {
    libc_errno = ENOMEM;
    return nullptr;
  }
  memory[0] = L'\0';
  AllocChecker ac;
  auto *file = new (ac) WideMemoryFile(buffer, size, memory);
  if (!ac) {
    ::free(memory);
    libc_errno = ENOMEM;
    return nullptr;
  }
  File::add_file(file);
  return reinterpret_cast<::FILE *>(file);
}
} // namespace LIBC_NAMESPACE_DECL
