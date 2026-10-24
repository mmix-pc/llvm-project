//===-- Validated message category text -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/messages_data.h"
#include "hdr/errno_macros.h"
#include "src/locale/category_validation.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<MessagesLocaleData>
MessagesLocaleData::read(const LocaleDataView &file) {
  auto yes = file.find(5, YES_EXPR);
  auto no = file.find(5, NO_EXPR);
  auto yesstr = file.find(5, YES_STRING);
  auto nostr = file.find(5, NO_STRING);
  if (!yes || !no || !yesstr || !nostr ||
      !locale_utf8_string(*yes, 0, size_t(-1)) ||
      !locale_utf8_string(*no, 0, size_t(-1)) ||
      !locale_utf8_string(*yesstr, 0, size_t(-1)) ||
      !locale_utf8_string(*nostr, 0, size_t(-1)))
    return Error(EINVAL);
  return MessagesLocaleData{*yes, *no, *yesstr, *nostr};
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
