//===-- Regional category admission -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_REGIONAL_H
#define LLVM_LIBC_SRC_LOCALE_REGIONAL_H

#include "src/locale/locale_file.h"
#include "src/locale/locale_storage.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// One immutable copy per selected name within a transaction, not a permanent
// cache. Category references keep accepted copies alive after this scope ends.
class RegionalLocaleFiles {
  LocaleDataFile *files[LOCALE_CATEGORY_COUNT]{};
  unsigned count = 0;

public:
  RegionalLocaleFiles() = default;
  RegionalLocaleFiles(const RegionalLocaleFiles &) = delete;
  RegionalLocaleFiles &operator=(const RegionalLocaleFiles &) = delete;
  ~RegionalLocaleFiles();
  ErrorOr<__locale_data *> load(unsigned category, cpp::string_view name);
};

ErrorOr<__locale_data *> create_regional_category(LocaleDataFile &file,
                                                  unsigned category);
// Compile both response expressions in the candidate's character/collation
// context before publication; no process-global locale change is performed.
int validate_locale_messages(__locale_t &locale);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_SRC_LOCALE_REGIONAL_H
