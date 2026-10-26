//===-- Message selection and recoding --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LIBINTL_MESSAGE_TRANSLATION_H
#define LLVM_LIBC_SRC_LIBINTL_MESSAGE_TRANSLATION_H

#include "src/__support/CPP/optional.h"
#include "src/libintl/mo_catalog.h"
#include "src/locale/transliteration_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
class LocaleDataFile;

// Select and copy one singular/plural message. The caller owns the returned
// allocation (free), independent of the catalog and conversion descriptor.
// Missing/malformed resources are errors for the public layer to turn into its
// exact msgid fallback. No global binding or locale state is modified.
ErrorOr<char *>
translate_message(const MoCatalogView &catalog, cpp::string_view key,
                  cpp::optional<unsigned long> count,
                  cpp::string_view output_codeset,
                  LocaleDataFile *mapping = nullptr,
                  const TransliterationData *transliteration = nullptr,
                  size_t *output_size = nullptr);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
