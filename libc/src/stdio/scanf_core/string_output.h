//===-- Assignment storage for scanf strings -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDIO_SCANF_CORE_STRING_OUTPUT_H
#define LLVM_LIBC_SRC_STDIO_SCANF_CORE_STRING_OUTPUT_H

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/func/realloc.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/scanf_core/core_structs.h"

namespace LIBC_NAMESPACE_DECL {
namespace scanf_core {

// An allocated assignment is published only after the entire conversion has
// succeeded. Consequently EOF (which precedes the first completed conversion)
// cannot leave a published allocation; failed conversions and unwind free here.
template <typename T> class StringOutput {
  const FormatSection &section;
  T *data = nullptr;
  size_t size = 0;
  size_t capacity = 0;

  bool allocated() const { return (section.flags & ALLOCATE) != 0; }

public:
  explicit StringOutput(const FormatSection &section) : section(section) {
    if (!allocated())
      data = static_cast<T *>(section.output_ptr);
  }
  StringOutput(const StringOutput &) = delete;
  StringOutput &operator=(const StringOutput &) = delete;
  ~StringOutput() {
    if (allocated())
      free(data);
  }

  bool append(T value) {
    if ((section.flags & NO_WRITE) != 0)
      return true;
    if (allocated() && size == capacity) {
      constexpr size_t max = cpp::numeric_limits<size_t>::max() / sizeof(T);
      if (capacity == max) {
        libc_errno = ENOMEM;
        return false;
      }
      size_t next = capacity == 0        ? 32
                    : capacity > max / 2 ? max
                                         : capacity * 2;
      void *replacement = realloc(data, next * sizeof(T));
      if (!replacement) {
        libc_errno = ENOMEM;
        return false;
      }
      data = static_cast<T *>(replacement);
      capacity = next;
    }
    data[size++] = value;
    return true;
  }

  void publish() {
    if (allocated() && (section.flags & NO_WRITE) == 0) {
      *static_cast<T **>(section.output_ptr) = data;
      data = nullptr;
    }
  }
};

} // namespace scanf_core
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_STDIO_SCANF_CORE_STRING_OUTPUT_H
