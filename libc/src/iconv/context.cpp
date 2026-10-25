//===-- Public conversion descriptor state --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/iconv/context.h"
#include "hdr/func/free.h"
#include "src/__support/alloc-checker.h"
#include "src/locale/builtin_transliteration.h"
#include "src/locale/locale.h"
#include "src/locale/locale_file.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<IconvContext *> IconvContext::open(const char *to, const char *from) {
  if (!to || !from)
    return Error(EINVAL);
  const auto *category = locale_snapshot(thread_locale).data[LC_CTYPE];
  const char *codeset = category->chinese_encoding ? "ISO-2022-CN"
                        : category->utf8         ? "UTF-8"
                                                 : "ISO-8859-1";
  if (!*from)
    from = codeset;
  if (!*to)
    to = codeset;
  auto names = EncodingConverter::parse_names(from, to);
  if (!names)
    return Error(names.error());
  struct Mapping {
    LocaleDataFile *file = nullptr;
    ~Mapping() {
      if (file)
        file->release();
    }
  } mapping;
  if (names->needs_mapping()) {
    auto file = load_chinese_encoding_file();
    if (!file)
      return Error(file.error());
    mapping.file = *file;
  }
  auto descriptor = EncodingDescriptor::create(from, to, mapping.file);
  if (!descriptor)
    return Error(descriptor.error());
  AllocChecker ac;
  auto *context = new (ac) IconvContext(*descriptor);
  if (!ac) {
    (*descriptor)->destroy();
    return Error(ENOMEM);
  }
  if (names->policy == EncodingConverter::NonIdentical::TRANSLIT) {
    auto rules = builtin_transliteration();
    if (!rules) {
      context->close();
      return Error(rules.error());
    }
    context->builtin = cpp::optional<TransliterationData>(*rules);
  }
  return context;
}

ErrorOr<size_t> IconvContext::convert(cpp::span<const uint8_t> &input,
                                      cpp::span<uint8_t> &output) {
  const TransliterationData *rules = nullptr;
  if (builtin) {
    // The selected locale is borrowed only during this call. Applications
    // synchronize global locale changes and keep selected objects alive.
    const auto *category = locale_snapshot(thread_locale).data[LC_CTYPE];
    rules = category->transliteration ? &*category->transliteration : &*builtin;
  }
  return descriptor->convert(input, output, rules);
}

void IconvContext::close() {
  this->~IconvContext();
  ::free(this);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
