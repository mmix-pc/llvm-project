//===-- Public conversion descriptor state -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_ICONV_CONTEXT_H
#define LLVM_LIBC_SRC_ICONV_CONTEXT_H

#include "src/__support/wchar/encoding_descriptor.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

class IconvContext {
public:
  static ErrorOr<IconvContext *> open(const char *to, const char *from);
  void close();
  ErrorOr<size_t> convert(cpp::span<const uint8_t> &input,
                          cpp::span<uint8_t> &output);
  ErrorOr<size_t> reset(cpp::span<uint8_t> *output) {
    return descriptor->reset(output);
  }
  IconvContext(const IconvContext &) = delete;
  IconvContext &operator=(const IconvContext &) = delete;

private:
  EncodingDescriptor *descriptor;
  cpp::optional<TransliterationData> builtin;
  explicit IconvContext(EncodingDescriptor *descriptor) : descriptor(descriptor) {}
  ~IconvContext() { descriptor->destroy(); }
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_ICONV_CONTEXT_H
