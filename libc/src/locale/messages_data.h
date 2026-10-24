//===-- Validated message category text -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_MESSAGES_DATA_H
#define LLVM_LIBC_SRC_LOCALE_MESSAGES_DATA_H

#include "src/locale/locale_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// The backing file must outlive these views. Expression syntax must be checked
// by the locale-aware regex compiler before the category can be published.
struct MessagesLocaleData {
  enum Key : uint32_t { YES_EXPR = 1, NO_EXPR, YES_STRING, NO_STRING };
  LocaleDataView::Item yesexpr;
  LocaleDataView::Item noexpr;
  LocaleDataView::Item yesstr;
  LocaleDataView::Item nostr;

  static ErrorOr<MessagesLocaleData> read(const LocaleDataView &file);
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_MESSAGES_DATA_H
