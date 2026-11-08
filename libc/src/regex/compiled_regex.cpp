//===-- Owned public regex state ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/regex/compiled_regex.h"
#ifdef LIBC_FULL_BUILD
#include "src/locale/locale_storage.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace internal {

CompiledRegex::Categories::~Categories() {
#ifdef LIBC_FULL_BUILD
  if (ctype)
    release_locale_category(ctype);
  if (collate)
    release_locale_category(collate);
#endif
}

int CompiledRegex::compile(cpp::string_view pattern, int flags) {
  const CollationData *collation = nullptr;
#ifdef LIBC_FULL_BUILD
  // regcomp publishes a fresh object only after compilation succeeds. The
  // application still synchronizes changes to the selected global locale.
  auto locale = locale_snapshot(thread_locale);
  if (!retain_locale_category(locale.__data[LC_CTYPE]))
    return REG_ESPACE;
  categories.ctype = locale.__data[LC_CTYPE];
  if (!retain_locale_category(locale.__data[LC_COLLATE]))
    return REG_ESPACE;
  categories.collate = locale.__data[LC_COLLATE];
  converter = categories.ctype->chinese_encoding
                  ? RestartableConverter(*categories.ctype->chinese_encoding)
                  : RestartableConverter(categories.ctype->utf8);
  if (categories.collate->collation)
    collation = &*categories.collate->collation;
#endif
  // Overlay builds preserve standalone UTF-8 conversion, without interpreting
  // another libc's opaque locale objects.
  return expression.compile(pattern, flags, converter, categories.ctype,
                            collation);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
