//===-- Bounded wide printf output -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_PRINTF_CORE_WIDE_WRITER_H
#define LLVM_LIBC_SRC___SUPPORT_PRINTF_CORE_WIDE_WRITER_H

#include "hdr/types/wchar_t.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/macros/config.h"
#include "src/__support/printf_core/core_structs.h"

namespace LIBC_NAMESPACE_DECL {
namespace printf_core {

// Capacity includes the terminator. Unlike snprintf, truncation is an error
// and the count describes only the wide characters actually stored.
class WideWriter {
  wchar_t *buffer;
  size_t capacity;
  size_t chars_written = 0;
  int error = WRITE_OK;

  LIBC_INLINE void terminate() {
    if (capacity != 0)
      buffer[chars_written] = L'\0';
  }

  LIBC_INLINE size_t writable_size(size_t requested) {
    if (error != WRITE_OK)
      return 0;
    if (requested >
        static_cast<size_t>(cpp::numeric_limits<int>::max()) - chars_written) {
      error = OVERFLOW_ERROR;
      return 0;
    }
    // A zero-capacity writer already has an error; reserve the terminator.
    size_t remaining = capacity - 1 - chars_written;
    if (requested > remaining) {
      error = BUFFER_TOO_SMALL;
      return remaining;
    }
    return requested;
  }

  template <typename CharT>
  LIBC_INLINE int write_span(cpp::basic_string_view<CharT> text) {
    size_t length = writable_size(text.size());
    for (size_t i = 0; i < length; ++i)
      buffer[chars_written + i] = static_cast<wchar_t>(text[i]);
    chars_written += length;
    terminate();
    return error;
  }

public:
  LIBC_INLINE WideWriter(wchar_t *buffer, size_t capacity)
      : buffer(buffer), capacity(capacity) {
    if (capacity == 0)
      error = BUFFER_TOO_SMALL;
    terminate();
  }

  LIBC_INLINE int write(cpp::basic_string_view<wchar_t> text) {
    return write_span(text);
  }

  LIBC_INLINE int write(wchar_t value, size_t length) {
    size_t written = writable_size(length);
    for (size_t i = 0; i < written; ++i)
      buffer[chars_written + i] = value;
    chars_written += written;
    terminate();
    return error;
  }

  LIBC_INLINE int write(wchar_t value) { return write(value, 1); }

  // Only basic ASCII fragments produced by numeric converters belong here.
  // Multibyte user strings require conversion before entering the writer.
  LIBC_INLINE int write_ascii(cpp::string_view text) {
    return write_span(text);
  }

  LIBC_INLINE size_t get_chars_written() const { return chars_written; }
  LIBC_INLINE int get_error() const { return error; }
};

// Numeric converters produce ASCII, independently of the format character type.
class WideNumericWriter {
  WideWriter &writer;

public:
  LIBC_INLINE explicit WideNumericWriter(WideWriter &writer) : writer(writer) {}
  LIBC_INLINE int write(cpp::string_view text) {
    return writer.write_ascii(text);
  }
  LIBC_INLINE int write(char value, size_t length) {
    return writer.write(static_cast<wchar_t>(value), length);
  }
  LIBC_INLINE int write(char value) { return write(value, 1); }
  LIBC_INLINE size_t get_chars_written() const {
    return writer.get_chars_written();
  }
};

} // namespace printf_core
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_PRINTF_CORE_WIDE_WRITER_H
