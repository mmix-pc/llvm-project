//===-- Owned encoding conversion state -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_WCHAR_ENCODING_DESCRIPTOR_H
#define LLVM_LIBC_SRC___SUPPORT_WCHAR_ENCODING_DESCRIPTOR_H

#include "src/__support/CPP/optional.h"
#include "src/__support/wchar/encoding_converter.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Stable-address conversion state retaining its immutable encoding tables.
// File discovery and current-locale acquisition belong to the public layer.
class EncodingDescriptor {
public:
  // The caller retains mapping_file throughout this call. Stateless encodings
  // need no file. A descriptor is independently mutable, not concurrently
  // usable.
  static ErrorOr<EncodingDescriptor *>
  create(cpp::string_view from, cpp::string_view to,
         LocaleDataFile *mapping_file = nullptr);
  EncodingDescriptor(const EncodingDescriptor &) = delete;
  EncodingDescriptor &operator=(const EncodingDescriptor &) = delete;
  void destroy();

  // Rules are borrowed only for this call, never captured at descriptor open.
  ErrorOr<size_t> convert(cpp::span<const uint8_t> &input,
                          cpp::span<uint8_t> &output,
                          const TransliterationData *rules = nullptr) {
    return converter->convert(input, output, rules);
  }
  ErrorOr<size_t> reset(cpp::span<uint8_t> *output) {
    return converter->reset(output);
  }

private:
  LocaleDataFile *file = nullptr;
  cpp::optional<ChineseEncodingData> mapping;
  cpp::optional<EncodingConverter> converter;
  EncodingDescriptor() = default;
  ~EncodingDescriptor();
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_WCHAR_ENCODING_DESCRIPTOR_H
