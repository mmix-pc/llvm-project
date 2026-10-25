//===-- ISO-2022-CN conversion state ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_WCHAR_ISO2022_CN_H
#define LLVM_LIBC_SRC___SUPPORT_WCHAR_ISO2022_CN_H

#include "src/__support/wchar/mbstate.h"
#include "src/locale/encoding_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

class ISO2022CNState {
public:
  enum class Event { INCOMPLETE, SHIFT, SCALAR };
  struct Step {
    Event event;
    uint32_t scalar;
  };

  bool initial() const { return boundary() && g1 == NONE && flags == 0; }
  bool boundary() const { return phase == BOUNDARY; }

  // Serialize named fields only: public mbstate_t padding is not state. The
  // phase tag separates this representation from UTF-8 and pending UTF-16.
  static ErrorOr<ISO2022CNState> load(const mbstate &saved);
  mbstate save() const;

  // A successful step consumes one byte. Invalid input leaves the state
  // unchanged. Callers distinguish restartable prefix consumption from iconv's
  // rollback to the last complete character or shift.
  ErrorOr<Step> push(uint8_t byte, const ChineseEncodingData &data);

  // Restartable decoding consumes an incomplete prefix into this state and
  // returns size_t(-2). Successful counts cover this call only; NUL returns 0.
  // iconv instead uses push() and rolls back an incomplete sequence.
  ErrorOr<size_t> decode(uint32_t *scalar, cpp::span<const uint8_t> input,
                         const ChineseEncodingData &data);

  // These operations commit state and output together, or change neither.
  ErrorOr<size_t> encode(uint32_t scalar, cpp::span<uint8_t> output,
                         const ChineseEncodingData &data);
  ErrorOr<size_t> reset(cpp::span<uint8_t> output);

private:
  enum G1 : uint8_t { NONE, GB, CNS };
  enum Phase : uint8_t {
    BOUNDARY,
    ESCAPE,
    DOLLAR,
    DESIGNATE_G1,
    DESIGNATE_G2,
    PAIR,
    SS2_FIRST,
    SS2_SECOND
  };
  static constexpr uint8_t G2 = 1, SHIFTED = 2;
  G1 g1 = NONE;
  Phase phase = BOUNDARY;
  uint8_t flags = 0;
  uint8_t row = 0;
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_WCHAR_ISO2022_CN_H
