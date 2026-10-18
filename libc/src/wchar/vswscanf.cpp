//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/vswscanf.h"
#include "src/__support/common.h"
#include "src/stdio/scanf_core/wide_scanf_main.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
class StringSource {
  const wchar_t *next;

public:
  explicit StringSource(const wchar_t *buffer) : next(buffer) {}
  wint_t get() { return *next ? wint_t(*next++) : WEOF; }
  void putback(wint_t) {}
};
} // namespace

LLVM_LIBC_FUNCTION(int, vswscanf,
                   (const wchar_t *__restrict buffer,
                    const wchar_t *__restrict format, va_list args)) {
  internal::ArgList list(args);
  StringSource source(buffer);
  return scanf_core::wide_scanf_main(source, format, list);
}
} // namespace LIBC_NAMESPACE_DECL
