//===-- Stateless scalar encoding primitives --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_WCHAR_SCALAR_CODEC_H
#define LLVM_LIBC_SRC___SUPPORT_WCHAR_SCALAR_CODEC_H

#include "hdr/stdint_proxy.h"
#include "src/__support/CPP/span.h"
#include "src/__support/error_or.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

enum class ScalarEncoding {
  ASCII,
  LATIN1,
  UTF8,
  UTF16LE,
  UTF16BE,
  UTF32LE,
  UTF32BE
};

struct DecodedScalar {
  uint32_t value;
  size_t bytes;
};

// Decode exactly one scalar, without consuming a partial character or changing
// caller state. Explicit-endian codecs treat U+FEFF as ordinary data.
ErrorOr<DecodedScalar> decode_scalar(ScalarEncoding encoding,
                                     cpp::span<const uint8_t> input);

// No output is changed on error. EILSEQ includes valid but unrepresentable
// scalars; the caller owns nonidentical-conversion policy and accounting.
ErrorOr<size_t> encode_scalar(ScalarEncoding encoding, uint32_t scalar,
                              cpp::span<uint8_t> output);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_WCHAR_SCALAR_CODEC_H
