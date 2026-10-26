//===-- Restartable locale character conversion --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/wchar/restartable_converter.h"
#include "src/__support/wchar/character_converter.h"
#include "src/__support/wchar/scalar_codec.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

RestartableConverter RestartableConverter::current() {
  if (auto *mapping = locale_chinese_encoding())
    return RestartableConverter(*mapping);
  return RestartableConverter(locale_is_utf8());
}

bool RestartableConverter::initial(const mbstate &state) const {
  return state.partial == 0 && state.bytes_stored == 0 &&
         state.total_bytes == 0;
}

bool RestartableConverter::valid(const mbstate &state) const {
  if (mapping)
    return ISO2022CNState::load(state).has_value();
  if (initial(state))
    return true;
  if (!utf8 || state.bytes_stored == 0 ||
      state.bytes_stored >= state.total_bytes)
    return false;
  // These are the scalar-prefix ranges after each accepted UTF-8 byte.
  // Exclude states which cannot have arisen from the strict decoder.
  uint32_t prefix = state.partial;
  switch (state.total_bytes) {
  case 2:
    return prefix >= 2 && prefix <= 0x1f;
  case 3:
    return state.bytes_stored == 1 ? prefix <= 0xf
                                   : prefix >= 0x20 && prefix <= 0x3ff &&
                                         !(prefix >= 0x360 && prefix <= 0x37f);
  case 4:
    if (state.bytes_stored == 1)
      return prefix <= 4;
    if (state.bytes_stored == 2)
      return prefix >= 0x10 && prefix <= 0x10f;
    return prefix >= 0x400 && prefix <= 0x43ff;
  default:
    return false;
  }
}

bool RestartableConverter::boundary(const mbstate &state) const {
  if (!mapping)
    return initial(state);
  auto saved = ISO2022CNState::load(state);
  return saved && saved->boundary();
}

ErrorOr<size_t> RestartableConverter::decode(uint32_t *scalar,
                                             cpp::span<const uint8_t> input,
                                             mbstate &state) const {
  if (mapping) {
    auto converter = ISO2022CNState::load(state);
    if (!converter)
      return Error(converter.error());
    auto result = converter->decode(scalar, input, *mapping);
    state = converter->save();
    return result;
  }
  if (!valid(state))
    return Error(EILSEQ);
  CharacterConverter converter(&state, utf8);
  for (size_t i = 0; i != input.size(); ++i) {
    if (int error = converter.push(char8_t(input[i])))
      return Error(error);
    if (converter.isFull()) {
      uint32_t value = converter.pop_utf32().value();
      if (scalar)
        *scalar = value;
      return value == 0 ? 0 : i + 1;
    }
  }
  return size_t(-2);
}

ErrorOr<size_t> RestartableConverter::encode(uint32_t scalar,
                                             cpp::span<uint8_t> output,
                                             mbstate &state) const {
  if (mapping) {
    auto converter = ISO2022CNState::load(state);
    if (!converter)
      return Error(converter.error());
    auto result = converter->encode(scalar, output, *mapping);
    if (result)
      state = converter->save();
    return result;
  }
  if (!initial(state))
    return Error(EILSEQ);
  return encode_scalar(utf8 ? ScalarEncoding::UTF8 : ScalarEncoding::LATIN1,
                       scalar, output);
}

ErrorOr<size_t> RestartableConverter::prefix(const mbstate &state,
                                             cpp::span<uint8_t> output) const {
  if (!mapping)
    return initial(state) ? ErrorOr<size_t>(0) : Error(EINVAL);
  auto saved = ISO2022CNState::load(state);
  if (!saved)
    return Error(saved.error());
  return saved->prefix(output);
}

ErrorOr<size_t>
RestartableConverter::transition(const mbstate &required, mbstate &current,
                                 cpp::span<uint8_t> output) const {
  if (!mapping)
    return initial(required) && initial(current) ? ErrorOr<size_t>(0)
                                                 : Error(EINVAL);
  auto target = ISO2022CNState::load(required);
  auto saved = ISO2022CNState::load(current);
  if (!target || !saved)
    return Error(EILSEQ);
  auto length = saved->transition(*target, output);
  if (length)
    current = saved->save();
  return length;
}

int RestartableConverter::save_unit(mbstate &state, UTF16Unit kind,
                                    uint32_t unit) const {
  uint32_t base = kind == UTF16Unit::HIGH ? 0xd800 : 0xdc00;
  if (unit < base || unit > base + 0x3ff)
    return EILSEQ;
  if (mapping) {
    auto loaded = ISO2022CNState::load(state);
    if (!loaded || !loaded->boundary())
      return EILSEQ;
  } else if (!initial(state)) {
    return EILSEQ;
  }
  state.partial = char32_t(unit);
  state.bytes_stored = uint8_t(kind);
  return 0;
}

ErrorOr<uint32_t> RestartableConverter::take_unit(mbstate &state,
                                                  UTF16Unit kind) const {
  uint32_t unit = state.partial;
  uint32_t base = kind == UTF16Unit::HIGH ? 0xd800 : 0xdc00;
  if (!pending(state, kind) || unit < base || unit > base + 0x3ff)
    return Error(EILSEQ);
  // Preserve designations and locking shift across the second UTF-16 unit.
  mbstate restored{};
  if (mapping && state.total_bytes != 0) {
    restored.bytes_stored = 0x80;
    restored.total_bytes = state.total_bytes;
  } else if (state.total_bytes != 0) {
    return Error(EILSEQ);
  }
  if (!valid(restored))
    return Error(EILSEQ);
  state = restored;
  return unit;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
