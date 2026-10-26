//===-- Restartable locale character conversion -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_WCHAR_RESTARTABLE_CONVERTER_H
#define LLVM_LIBC_SRC___SUPPORT_WCHAR_RESTARTABLE_CONVERTER_H

#include "src/__support/wchar/iso2022_cn.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

inline constexpr size_t MAX_LOCALE_CHARACTER_BYTES = 8;

// The caller retains the immutable category data separately from mbstate_t.
// Unlike iconv, an incomplete input prefix is saved rather than rolled back.
class RestartableConverter {
public:
  explicit RestartableConverter(bool utf8) : utf8(utf8), mapping(nullptr) {}
  explicit RestartableConverter(const ChineseEncodingData &mapping)
      : utf8(false), mapping(&mapping) {}

  static RestartableConverter current();

  bool valid(const mbstate &state) const;
  bool initial(const mbstate &state) const;
  // Completed shift sequences may leave a noninitial state at a character
  // boundary. EOF there is not an incomplete-character error.
  bool boundary(const mbstate &state) const;
  ErrorOr<size_t> decode(uint32_t *scalar, cpp::span<const uint8_t> input,
                         mbstate &state) const;
  // Capacity failure leaves both output and saved state unchanged. NUL emits
  // the required reset bytes and counts them; null pointers are wrapper policy.
  ErrorOr<size_t> encode(uint32_t scalar, cpp::span<uint8_t> output,
                         mbstate &state) const;

  enum class UTF16Unit : uint8_t { HIGH = 0x90, LOW = 0x91 };
  static bool pending(const mbstate &state, UTF16Unit unit) {
    return state.bytes_stored == uint8_t(unit);
  }
  int save_unit(mbstate &state, UTF16Unit kind, uint32_t unit) const;
  ErrorOr<uint32_t> take_unit(mbstate &state, UTF16Unit kind) const;

private:
  bool utf8;
  const ChineseEncodingData *mapping;
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
