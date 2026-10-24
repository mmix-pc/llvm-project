//===-- ISO-2022-CN conversion state --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/wchar/iso2022_cn.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

static_assert(sizeof(ISO2022CNState) == 4);

ErrorOr<ISO2022CNState::Step>
ISO2022CNState::push(uint8_t byte, const ChineseEncodingData &data) {
  ISO2022CNState next = *this;
  Step result{Event::INCOMPLETE, 0};
  switch (phase) {
  case BOUNDARY:
    if (byte >= 0x80)
      return Error(EILSEQ);
    if (byte == 0x1b) {
      next.phase = ESCAPE;
    } else if (byte == 0x0e) {
      if (g1 == NONE)
        return Error(EILSEQ);
      next.flags |= SHIFTED;
      result.event = Event::SHIFT;
    } else if (byte == 0x0f) {
      next.flags &= ~SHIFTED;
      result.event = Event::SHIFT;
    } else if (byte == 0) {
      next = {};
      result = {Event::SCALAR, 0};
    } else if (flags & SHIFTED) {
      if (byte < 0x21 || byte > 0x7e)
        return Error(EILSEQ);
      next.row = byte;
      next.phase = PAIR;
    } else {
      if (byte == '\n')
        next = {};
      result = {Event::SCALAR, byte};
    }
    break;
  case ESCAPE:
    if (byte == '$') {
      next.phase = DOLLAR;
    } else if (byte == 'N' && (flags & G2)) {
      next.phase = SS2_FIRST;
    } else {
      return Error(EILSEQ);
    }
    break;
  case DOLLAR:
    if (byte == ')')
      next.phase = DESIGNATE_G1;
    else if (byte == '*')
      next.phase = DESIGNATE_G2;
    else
      return Error(EILSEQ);
    break;
  case DESIGNATE_G1:
    if (byte == 'A')
      next.g1 = GB;
    else if (byte == 'G')
      next.g1 = CNS;
    else
      return Error(EILSEQ);
    next.phase = BOUNDARY;
    result.event = Event::SHIFT;
    break;
  case DESIGNATE_G2:
    if (byte != 'H')
      return Error(EILSEQ);
    next.flags |= G2;
    next.phase = BOUNDARY;
    result.event = Event::SHIFT;
    break;
  case SS2_FIRST:
    if (byte < 0x21 || byte > 0x7e)
      return Error(EILSEQ);
    next.row = byte;
    next.phase = SS2_SECOND;
    break;
  case PAIR:
  case SS2_SECOND: {
    if (byte < 0x21 || byte > 0x7e)
      return Error(EILSEQ);
    auto set = phase == SS2_SECOND ? ChineseEncodingData::CNS2
               : g1 == GB          ? ChineseEncodingData::GB2312
                                   : ChineseEncodingData::CNS1;
    auto scalar = data.decode(set, (uint32_t(row) << 8) | byte);
    if (!scalar)
      return Error(scalar.error());
    next.phase = BOUNDARY;
    next.row = 0;
    result = {Event::SCALAR, *scalar};
    break;
  }
  }
  *this = next;
  return result;
}

ErrorOr<size_t> ISO2022CNState::encode(uint32_t scalar,
                                       cpp::span<uint8_t> output,
                                       const ChineseEncodingData &data) {
  if (!boundary())
    return Error(EINVAL);
  ISO2022CNState next = *this;
  uint8_t bytes[8];
  size_t count = 0;
  if (scalar < 0x80) {
    if (scalar == 0x1b || scalar == 0x0e || scalar == 0x0f)
      return Error(EILSEQ);
    if (flags & SHIFTED)
      bytes[count++] = 0x0f;
    bytes[count++] = uint8_t(scalar);
    next.flags &= ~SHIFTED;
    if (scalar == 0 || scalar == '\n')
      next = {};
  } else {
    auto selected =
        g1 == CNS ? ChineseEncodingData::CNS1 : ChineseEncodingData::GB2312;
    auto position = data.encode(selected, scalar);
    if (g1 == NONE || !position) {
      for (unsigned i = 0; i != ChineseEncodingData::SET_COUNT; ++i) {
        selected = ChineseEncodingData::Set(i);
        position = data.encode(selected, scalar);
        if (position)
          break;
      }
    }
    if (!position)
      return Error(EILSEQ);
    if (selected == ChineseEncodingData::CNS2) {
      if (!(flags & G2)) {
        bytes[count++] = 0x1b;
        bytes[count++] = '$';
        bytes[count++] = '*';
        bytes[count++] = 'H';
      }
      bytes[count++] = 0x1b;
      bytes[count++] = 'N';
      next.flags |= G2;
    } else {
      G1 chosen = selected == ChineseEncodingData::GB2312 ? GB : CNS;
      if (g1 != chosen) {
        bytes[count++] = 0x1b;
        bytes[count++] = '$';
        bytes[count++] = ')';
        bytes[count++] = chosen == GB ? 'A' : 'G';
      }
      if (!(flags & SHIFTED))
        bytes[count++] = 0x0e;
      next.g1 = chosen;
      next.flags |= SHIFTED;
    }
    bytes[count++] = uint8_t(*position >> 8);
    bytes[count++] = uint8_t(*position);
  }
  if (output.size() < count)
    return Error(E2BIG);
  for (size_t i = 0; i != count; ++i)
    output[i] = bytes[i];
  *this = next;
  return count;
}

ErrorOr<size_t> ISO2022CNState::reset(cpp::span<uint8_t> output) {
  if (!boundary())
    return Error(EINVAL);
  size_t count = (flags & SHIFTED) ? 1 : 0;
  if (output.size() < count)
    return Error(E2BIG);
  if (count)
    output[0] = 0x0f;
  *this = {};
  return count;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
