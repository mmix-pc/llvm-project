//===-- Embedded neutral transliteration ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/builtin_transliteration.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<TransliterationData> builtin_transliteration() {
  static constexpr char BYTES[] =
#include "src/locale/builtin_transliteration.inc"
      ;
  auto file = LocaleDataView::parse(
      {reinterpret_cast<const uint8_t *>(BYTES), sizeof(BYTES) - 1});
  if (!file)
    return Error(file.error());
  return TransliterationData::read(*file);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
